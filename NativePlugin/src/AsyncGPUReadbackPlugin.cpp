#include <cstddef>
#include <vector>
#include <map>
#include <mutex>
#include <memory>
#include <cstring>
#include "Unity/IUnityInterface.h"
#include "Unity/IUnityGraphics.h"
#include <iostream>
#include "TypeHelpers.hpp"
#include <string>
#include <atomic>

#ifdef DEBUG
	#include <fstream>
	#include <thread>
#endif

struct BaseTask;
struct SsboTask;
struct FrameTask;

static IUnityGraphics* graphics = NULL;
static UnityGfxRenderer renderer = kUnityGfxRendererNull;

static std::map<int, std::shared_ptr<BaseTask>> tasks;
static std::vector<int> pending_release_tasks;
static std::mutex tasks_mutex;
int next_event_id = 1;
static bool inited = false;

extern "C" bool UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API CheckCompatible();
static void UNITY_INTERFACE_API OnGraphicsDeviceEvent(UnityGfxDeviceEventType eventType);

// Call this on a function parameter to suppress the unused paramter warning
template <class T> inline
void unused(T const & result) { static_cast<void>(result); }

struct BaseTask {
	//These vars might be accessed from both render thread and main thread. guard them.
	std::atomic<bool> initialized;
	std::atomic<bool> error;
	std::atomic<bool> done;
	/*Called in render thread*/
	virtual void StartRequest() = 0;
	virtual void Update() = 0;

	BaseTask() :
		initialized(false),
		error(false),
		done(false)
	{

	}

	virtual ~BaseTask()
	{
		if (result_data != nullptr) {
			delete[] result_data;
		}
	}
	
	char* GetData(size_t* length) {
		if (!done || error) {
			return nullptr;
		}
		std::lock_guard<std::mutex> guard(mainthread_data_mutex);
		if (this->result_data == nullptr) {
			return nullptr;
		}
		*length = result_data_length;
		return result_data;
	}

protected:
	/*
	* Called by subclass in Update, to commit data and mark as done.
	*/
	void FinishAndCommitData(char* dataPtr, size_t length) {
		std::lock_guard<std::mutex> guard(mainthread_data_mutex);
		if (this->result_data != nullptr) {
			//WTF
			return;
		}
		this->result_data = dataPtr;
		this->result_data_length = length;
		done = true;
	}

	/*
	Called by subclass to mark as error.
	*/
	void ErrorOut() {
		error = true;
		done = true;
	}
private:
	std::mutex mainthread_data_mutex;
	char* result_data = nullptr;
	size_t result_data_length = 0;
};

/*Task for readback from ssbo. Which is compute buffer in Unity
*/
struct SsboTask : public BaseTask {
	GLuint ssbo;
	GLuint pbo;
	GLsync fence;
	GLint bufferSize;
	void Init(GLuint _ssbo, GLint _bufferSize) {
		this->ssbo = _ssbo;
		this->bufferSize = _bufferSize;
	}

	virtual void StartRequest() override {
		//bind it to GL_COPY_WRITE_BUFFER to wait for use
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, this->ssbo);

		//Get our pbo ready.
		glGenBuffers(1, &pbo);
		glBindBuffer(GL_PIXEL_PACK_BUFFER, this->pbo);
		//Initialize pbo buffer storage.
		glBufferData(GL_PIXEL_PACK_BUFFER, bufferSize, 0, GL_STREAM_READ);

		//Copy data to pbo.
		glCopyBufferSubData(GL_SHADER_STORAGE_BUFFER, GL_PIXEL_PACK_BUFFER, 0, 0, bufferSize);

		//Unbind buffers.
		glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

		//Create a fence.
		fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
	}

	virtual void Update() override {
		// Check fence state
		GLint status = 0;
		GLsizei length = 0;
		glGetSynciv(fence, GL_SYNC_STATUS, sizeof(GLint), &length, &status);
		if (length <= 0) {
			ErrorOut();
			Cleanup();
			return;
		}

		// When it's done
		if (status == GL_SIGNALED) {

			// Bind back the pbo
			glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);

			// Map the buffer and copy it to data
			void* ptr = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, bufferSize, GL_MAP_READ_BIT);

			// Allocate the final data buffer !!! WARNING: free, will have to be done on script side !!!!
			char* data = new char[bufferSize];
			std::memcpy(data, ptr, bufferSize);
			FinishAndCommitData(data, bufferSize);

			// Unmap and unbind
			glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
			glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
			Cleanup();
		}
	}

	void Cleanup()
	{
		if (pbo != 0) {
			// Clear buffers
			glDeleteBuffers(1, &(pbo));
		}
		if (fence != 0) {
			glDeleteSync(fence);
		}
	}
};

/*Task for readback texture.
*/
struct FrameTask : public BaseTask {
	int size;
	GLsync fence;
	GLuint texture;
	GLuint fbo;
	GLuint pbo;
	int miplevel;
	int height;
	int width;
	int depth;
	GLint internal_format;
	virtual void StartRequest() override {
		// Get texture informations
		glBindTexture(GL_TEXTURE_2D, texture);
		glGetTexLevelParameteriv(GL_TEXTURE_2D, miplevel, GL_TEXTURE_WIDTH, &(width));
		glGetTexLevelParameteriv(GL_TEXTURE_2D, miplevel, GL_TEXTURE_HEIGHT, &(height));
		glGetTexLevelParameteriv(GL_TEXTURE_2D, miplevel, GL_TEXTURE_DEPTH, &(depth));
		glGetTexLevelParameteriv(GL_TEXTURE_2D, miplevel, GL_TEXTURE_INTERNAL_FORMAT, &(internal_format));
		int pixelBits = getPixelSizeFromInternalFormat(internal_format);
		size = depth * width * height * pixelBits / 8;
		// Check for errors
		if (size == 0
			|| pixelBits % 8 != 0	//Only support textures aligned to one byte.
			|| getFormatFromInternalFormat(internal_format) == 0
			|| getTypeFromInternalFormat(internal_format) == 0) {
			ErrorOut();
			return;
		}

		// Create the fbo (frame buffer object) from the given texture
		glGenFramebuffers(1, &(fbo));

		// Bind the texture to the fbo
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);
		glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, texture, 0);

		// Create and bind pbo (pixel buffer object) to fbo
		glGenBuffers(1, &(pbo));
		glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);
		glBufferData(GL_PIXEL_PACK_BUFFER, size, 0, GL_DYNAMIC_READ);

		// Start the read request
		glReadBuffer(GL_COLOR_ATTACHMENT0);
		glReadPixels(0, 0, width, height, getFormatFromInternalFormat(internal_format), getTypeFromInternalFormat(internal_format), 0);

		// Unbind buffers
		glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);

		// Fence to know when it's ready
		fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
	}

	virtual void Update() override {
		// Check fence state
		GLint status = 0;
		GLsizei length = 0;
		glGetSynciv(fence, GL_SYNC_STATUS, sizeof(GLint), &length, &status);
		if (length <= 0) {
			ErrorOut();
			Cleanup();
			return;
		}

		// When it's done
		if (status == GL_SIGNALED) {

			// Bind back the pbo
			glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);

			// Map the buffer and copy it to data

			char* data = new char[size];
			void* ptr = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, size, GL_MAP_READ_BIT);
			std::memcpy(data, ptr, size);
			FinishAndCommitData(data, size);

			// Unmap and unbind
			glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
			glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
			Cleanup();
		}
	}

	void Cleanup()
	{
		// Clear buffers
		if (fbo != 0)
			glDeleteFramebuffers(1, &(fbo));
		if (pbo != 0)
			glDeleteBuffers(1, &(pbo));
		if (fence != 0)
			glDeleteSync(fence);
	}
};

/**
 * Unity plugin load event
 */
extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API
    UnityPluginLoad(IUnityInterfaces* unityInterfaces)
{
    graphics = unityInterfaces->Get<IUnityGraphics>();
    graphics->RegisterDeviceEventCallback(OnGraphicsDeviceEvent);
        
    // Run OnGraphicsDeviceEvent(initialize) manually on plugin load
    // to not miss the event in case the graphics device is already initialized
    OnGraphicsDeviceEvent(kUnityGfxDeviceEventInitialize);

	if (CheckCompatible()) {
		inited = true;
		glewInit();
	}
}

/**
 * Unity unload plugin event
 */
extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API UnityPluginUnload()
{
	graphics->UnregisterDeviceEventCallback(OnGraphicsDeviceEvent);
}

/**
 * Called for every graphics device events
 */
static void UNITY_INTERFACE_API OnGraphicsDeviceEvent(UnityGfxDeviceEventType eventType)
{
	// Create graphics API implementation upon initialization
	if (eventType == kUnityGfxDeviceEventInitialize)
	{
		renderer = graphics->GetRenderer();
	}

	// Cleanup graphics API implementation upon shutdown
	if (eventType == kUnityGfxDeviceEventShutdown)
	{
		renderer = kUnityGfxRendererNull;
	}
}


/**
* Check if plugin is compatible with this system
* This plugin is only compatible with opengl core
*/
extern "C" bool UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API CheckCompatible() {
	return (renderer == kUnityGfxRendererOpenGLCore);
}

int InsertEvent(std::shared_ptr<BaseTask> task) {
	int event_id = next_event_id;
	next_event_id++;

	std::lock_guard<std::mutex> guard(tasks_mutex);
	tasks[event_id] = task;

	return event_id;
}

/**
* @brief Init of the make request action.
* You then have to call makeRequest_renderThread
* via GL.IssuePluginEvent with the returned event_id
*
* @param texture OpenGL texture id
* @return event_id to give to other functions and to IssuePluginEvent
*/
extern "C" int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API RequestTextureMainThread(GLuint texture, int miplevel) {
	// Create the task
	std::shared_ptr<FrameTask> task = std::make_shared<FrameTask>();
	task->texture = texture;
	task->miplevel = miplevel;
	return InsertEvent(task);
}

extern "C" int UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API RequestComputeBufferMainThread(GLuint computeBuffer, GLint bufferSize) {
	// Create the task
	std::shared_ptr<SsboTask> task = std::make_shared<SsboTask>();
	task->Init(computeBuffer, bufferSize);
	return InsertEvent(task);
}

/**
 * @brief Create a a read texture request
 * Has to be called by GL.IssuePluginEvent
 * @param event_id containing the the task index, given by makeRequest_mainThread
 */
extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API KickstartRequestInRenderThread(int event_id) {
	// Get task back
	std::lock_guard<std::mutex> guard(tasks_mutex);
	std::shared_ptr<BaseTask> task = tasks[event_id];
	task->StartRequest();
	// Done init
	task->initialized = true;
}

extern "C" UnityRenderingEvent UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API GetKickstartFunctionPtr() {
	return KickstartRequestInRenderThread;
}

/**
* Update all current available tasks. Should be called in render thread.
 */
extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API UpdateRenderThread(int event_id) {
	unused(event_id);
	//Lock up.
	std::lock_guard<std::mutex> guard(tasks_mutex);
	for (auto ite = tasks.begin(); ite != tasks.end(); ite++) {
		auto task = ite->second;
		if (task != nullptr && task->initialized && !task->done)
			task->Update();
	}
}

extern "C" UnityRenderingEvent UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API GetUpdateRenderThreadFunctionPtr() {
	return UpdateRenderThread;
}

/**
* Update in main thread.
* This will erase tasks that are marked as done in last frame.
* Also save tasks that are done this frame.
* By doing this, all tasks are done for one frame, then removed.
 */
extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API UpdateMainThread() {
	//Lock up.
	std::lock_guard<std::mutex> guard(tasks_mutex);

	//Remove tasks that are done in the last update.
	for (auto& event_id : pending_release_tasks) {
		auto t = tasks.find(event_id);
		if (t != tasks.end()) {
			tasks.erase(t);
		}
	}
	pending_release_tasks.clear();

	//Push new done tasks to pending list.
	for (auto ite = tasks.begin(); ite != tasks.end(); ite++) {
		auto task = ite->second;
		if (task->done) {
			pending_release_tasks.push_back(ite->first);
		}
	}
}

/**
 * @brief Get data from the main thread.
 * The data owner is still native plugin, outside caller should copy the data asap to avoid any problem.
 * 
 */
extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API GetData(int event_id, void** buffer, size_t* length) {
	// Get task back
	std::lock_guard<std::mutex> guard(tasks_mutex);
	std::shared_ptr<BaseTask> task = tasks[event_id];

	// Do something only if initialized (thread safety)
	if (!task->done) {
		return;
	}

	// Return the pointer.
	// The memory ownership doesn't transfer.
	auto dataPtr = task->GetData(length);
	*buffer = dataPtr;
}

/**
 * @brief Check if request exists
 * @param event_id containing the the task index, given by makeRequest_mainThread
 */
extern "C" bool UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API TaskExists(int event_id) {
	// Get task back
	std::lock_guard<std::mutex> guard(tasks_mutex);
	bool result = tasks.find(event_id) != tasks.end();

	return result;
}

/**
 * @brief Check if request is done
 * @param event_id containing the the task index, given by makeRequest_mainThread
 */
extern "C" bool UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API TaskDone(int event_id) {
	// Get task back
	std::lock_guard<std::mutex> guard(tasks_mutex);
	auto ite = tasks.find(event_id);
	if (ite != tasks.end())
		return ite->second->done;
	return true;	//If it's disposed, also assume it's done.
}

/**
 * @brief Check if request is in error
 * @param event_id containing the the task index, given by makeRequest_mainThread
 */
extern "C" bool UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API TaskError(int event_id) {
	// Get task back
	std::lock_guard<std::mutex> guard(tasks_mutex);
	auto ite = tasks.find(event_id);
	if (ite != tasks.end())
		return ite->second->error;

	return true;	//It's disposed, assume as error.
}