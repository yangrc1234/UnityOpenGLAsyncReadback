#include <cstddef>
#include <map>
#include <mutex>
#include <memory>
#include <cstring>
#include "Unity/IUnityInterface.h"
#include "Unity/IUnityGraphics.h"
#include <iostream>
#include "TypeHelpers.hpp"
#include <string>

#ifdef DEBUG
	#include <fstream>
	#include <thread>
#endif

extern "C" bool UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API isCompatible();
static void UNITY_INTERFACE_API OnGraphicsDeviceEvent(UnityGfxDeviceEventType eventType);


struct BaseTask {
	bool initialized = false;
	bool error = false;
	bool done = false;
	/*Called in render thread*/
	virtual void StartRequest() = 0;
	virtual void Update() = 0;
	virtual void* GetData(size_t* length) = 0;
	virtual void ReleaseData() = 0;
};

struct SsboTask : public BaseTask {
	GLuint ssbo;
	GLuint pbo;
	char* data = nullptr;
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

	virtual void Update() {
		// Check fence state
		GLint status = 0;
		GLsizei length = 0;
		glGetSynciv(fence, GL_SYNC_STATUS, sizeof(GLint), &length, &status);
		if (length <= 0) {
			error = true;
			done = true;
			return;
		}

		// When it's done
		if (status == GL_SIGNALED) {

			// Bind back the pbo
			glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);

			// Map the buffer and copy it to data
			void* ptr = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, bufferSize, GL_MAP_READ_BIT);

			// Allocate the final data buffer !!! WARNING: free, will have to be done on script side !!!!
			data = new char[bufferSize];
			std::memcpy(data, ptr, bufferSize);

			// Unmap and unbind
			glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
			glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

			// Clear buffers
			glDeleteBuffers(1, &(pbo));
			glDeleteSync(fence);

			// yeah task is done!
			done = true;
		}
	}

	virtual void* GetData(size_t* length) override {
		if (!done) {
			return nullptr;
		}
		
		*length = this->bufferSize;
		return data;
	}

	virtual void ReleaseData() override {
		if (data != nullptr) {
			delete[] data;
			data = nullptr;
		}
	}
};

struct FrameTask : public BaseTask {
	int size;
	GLsync fence;
	GLuint texture;
	GLuint fbo;
	GLuint pbo;
	char* data = nullptr;
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
		size = depth * width * height * getPixelSizeFromInternalFormat(internal_format);

		// Check for errors
		if (size == 0
			|| getFormatFromInternalFormat(internal_format) == 0
			|| getTypeFromInternalFormat(internal_format) == 0) {
			error = true;
		}

		// Allocate the final data buffer !!! WARNING: free, will have to be done on script side !!!!
		data = new char[size];

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
			error = true;
			done = true;
			return;
		}

		// When it's done
		if (status == GL_SIGNALED) {

			// Bind back the pbo
			glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);

			// Map the buffer and copy it to data
			void* ptr = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, size, GL_MAP_READ_BIT);
			std::memcpy(data, ptr, size);

			// Unmap and unbind
			glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
			glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

			// Clear buffers
			glDeleteFramebuffers(1, &(fbo));
			glDeleteBuffers(1, &(pbo));
			glDeleteSync(fence);

			// yeah task is done!
			done = true;
		}
	}

	virtual void* GetData(size_t* length) {
		if (!done) {
			return nullptr;
		}
		*length = size;
		return data;
	}

	virtual void ReleaseData() override {
		if (data != nullptr) {
			delete[] data;
			data = nullptr;
		}
	}
};

static IUnityInterfaces* unity_interfaces = NULL;
static IUnityGraphics* graphics = NULL;
static UnityGfxRenderer renderer = kUnityGfxRendererNull;

static std::map<int,std::shared_ptr<BaseTask>> tasks;
static std::mutex tasks_mutex;
int next_event_id = 1;
static bool inited = false;

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

	if (isCompatible()) {
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
extern "C" bool UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API isCompatible() {
	return (renderer == kUnityGfxRendererOpenGLCore);
}


int InsertEvent(std::shared_ptr<BaseTask> task) {
	int event_id = next_event_id;
	next_event_id++;

	// Save it (lock because possible vector resize)
	tasks_mutex.lock();
	tasks[event_id] = task;
	tasks_mutex.unlock();

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
	tasks_mutex.lock();
	std::shared_ptr<BaseTask> task = tasks[event_id];
	tasks_mutex.unlock();
	task->StartRequest();
	// Done init
	task->initialized = true;
}

extern "C" UnityRenderingEvent UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API GetKickstartFunctionPtr() {
	return KickstartRequestInRenderThread;
}

/**
 * @brief check if data is ready
 * Has to be called by GL.IssuePluginEvent
 * @param event_id containing the the task index, given by makeRequest_mainThread
 */
extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API update_renderThread(int event_id) {
	// Get task back
	tasks_mutex.lock();
	std::shared_ptr<BaseTask> task = tasks[event_id];
	tasks_mutex.unlock();

	// Check if task has not been already deleted by main thread
	if(task == nullptr) {
		return;
	}

	// Do something only if initialized (thread safety)
	if (!task->initialized || task->done) {
		return;
	}

	task->Update();
}

extern "C" UnityRenderingEvent UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API getfunction_update_renderThread() {
	return update_renderThread;
}

/**
 * @brief Get data from the main thread. The caller owns the data pointer now.
 * @param event_id containing the the task index, given by makeRequest_mainThread
 */
extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API getData_mainThread(int event_id, void** buffer, size_t* length) {
	// Get task back
	tasks_mutex.lock();
	std::shared_ptr<BaseTask> task = tasks[event_id];
	tasks_mutex.unlock();

	// Do something only if initialized (thread safety)
	if (!task->done) {
		return;
	}

	// Copy the pointer. Warning: free will have to be done on script side
	auto dataPtr = task->GetData(length);
	*buffer = dataPtr;
}

/**
 * @brief Check if request exists
 * @param event_id containing the the task index, given by makeRequest_mainThread
 */
extern "C" bool UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API RequestExists(int event_id) {
	// Get task back
	tasks_mutex.lock();
	bool result = tasks.find(event_id) != tasks.end();
	tasks_mutex.unlock();

	return result;
}

/**
 * @brief Check if request is done
 * @param event_id containing the the task index, given by makeRequest_mainThread
 */
extern "C" bool UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API isRequestDone(int event_id) {
	// Get task back
	tasks_mutex.lock();
	std::shared_ptr<BaseTask> task = tasks[event_id];
	tasks_mutex.unlock();

	return task->done;
}

/**
 * @brief Check if request is in error
 * @param event_id containing the the task index, given by makeRequest_mainThread
 */
extern "C" bool UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API isRequestError(int event_id) {
	// Get task back
	tasks_mutex.lock();
	std::shared_ptr<BaseTask> task = tasks[event_id];
	tasks_mutex.unlock();

	return task->error;
}

/**
 * @brief clear data for a frame
 * Warning : Buffer is never cleaned, it has to be cleaned from script side
 * Has to be called by GL.IssuePluginEvent
 * @param event_id containing the the task index, given by makeRequest_mainThread
 */
extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API dispose(int event_id) {
	// Remove from tasks
	tasks_mutex.lock();
	std::shared_ptr<BaseTask> task = tasks[event_id];
	tasks.erase(event_id);
	task->ReleaseData();
	tasks_mutex.unlock();
}