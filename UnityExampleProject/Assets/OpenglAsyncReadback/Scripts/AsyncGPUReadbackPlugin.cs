using UnityEngine;
using System.Collections;
using System;
using System.Threading;
using System.Runtime.InteropServices;
using UnityEngine.Rendering;
using Unity.Collections;
using Unity.Collections.LowLevel.Unsafe;

namespace Yangrc.OpenGLAsyncReadback {

    /// <summary>
    /// Helper struct that wraps unity async readback and our opengl readback together, to hide difference
    /// </summary>
    public struct UniversalAsyncGPUReadbackRequest {

        /// <summary>
        /// Request readback of a texture.
        /// </summary>
        /// <param name="src"></param>
        /// <param name="mipmapIndex"></param>
        /// <returns></returns>
        public static UniversalAsyncGPUReadbackRequest Request(Texture src, int mipmapIndex = 0) {
            if (SystemInfo.supportsAsyncGPUReadback) {

                return new UniversalAsyncGPUReadbackRequest() {
                    isPlugin = false,
                    uDisposd = false,
                    uRequest = AsyncGPUReadback.Request(src, mipIndex: mipmapIndex),
                };
            } else {
                return new UniversalAsyncGPUReadbackRequest() {
                    isPlugin = true,
                    oRequest = OpenGLAsyncReadbackRequest.CreateTextureRequest((int)src.GetNativeTexturePtr(), 0)
                };  
            }
        }

        public static UniversalAsyncGPUReadbackRequest Request(ComputeBuffer computeBuffer) {
            if (SystemInfo.supportsAsyncGPUReadback) {
                return new UniversalAsyncGPUReadbackRequest() {
                    isPlugin = false,
                    uInited = true,
                    uDisposd = false,
                    uRequest = AsyncGPUReadback.Request(computeBuffer),
                };
            } else {
                return new UniversalAsyncGPUReadbackRequest() {
                    isPlugin = true,
                    oRequest = OpenGLAsyncReadbackRequest.CreateTextureRequest((int)computeBuffer.GetNativeBufferPtr(), 0),
                };
            }
        }

        public static UniversalAsyncGPUReadbackRequest OpenGLRequestTexture(int texture, int mipmapIndex) {
            return new UniversalAsyncGPUReadbackRequest() {
                isPlugin = true,
                oRequest = OpenGLAsyncReadbackRequest.CreateTextureRequest((int)texture, 0)
            };
        }

        public static UniversalAsyncGPUReadbackRequest OpenGLRequestComputeBuffer(int computeBuffer, int size) {
            return new UniversalAsyncGPUReadbackRequest() {
                isPlugin = true,
                oRequest = OpenGLAsyncReadbackRequest.CreateComputeBufferRequest((int)computeBuffer, size)
            };
        }


        public void Update() {
            if (isPlugin) {
                oRequest.Update();
            } else {
                uRequest.Update();
            }
        }

        public bool done {
            get {
                if (isPlugin) {
                    return oRequest.done;
                } else {
                    return uRequest.done;
                }
            }
        }

        public bool hasError {
            get {
                if (isPlugin) {
                    return oRequest.hasError;
                } else {
                    return uRequest.hasError;
                }
            }
        }

        /// <summary>
        /// Release memory of completed request.  
        /// <para>
        /// When used in unity's readback, this method just marks the request as disposed, since unity will handle it.
        /// </para>
        /// </summary>
        public void Dispose() {
            if (isPlugin) {
                oRequest.Dispose();
            } else {
                uDisposd = true;
                //Don't care.
            }
        }

        /// <summary>
        /// Get data of a readback request.  
        /// The data is allocated as temp, so it only stay alive for one frame.
        /// </summary>
        /// <typeparam name="T"></typeparam>
        /// <returns></returns>
        public NativeArray<T> GetData<T>()  where T:struct{
            if (isPlugin) {
                return oRequest.GetRawData<T>();
            } else {
                return uRequest.GetData<T>();
            }
        }

        public bool valid {
            get {
                return isPlugin ? oRequest.Valid() : (!uDisposd && uInited);
            }
        }

        public bool isPlugin { get; private set; }

        //fields for unity request.
        private bool uInited;
        private bool uDisposd;
        private AsyncGPUReadbackRequest uRequest;

        //fields for opengl request.
        private OpenGLAsyncReadbackRequest oRequest;

    }

	internal struct OpenGLAsyncReadbackRequest {
        public static bool IsAvailable() {
            return SystemInfo.graphicsDeviceType == GraphicsDeviceType.OpenGLCore;  //Not tested on es3 yet.
        }
        /// <summary>
        /// Identify native task object handling the request.
        /// </summary>
        private int nativeTaskHandle;

		/// <summary>
		/// Check if the request is done
		/// </summary>
		public bool done
	    {
	        get {
                AssertRequestValid();
                return isRequestDone(nativeTaskHandle);
            }
	    }

		/// <summary>
		/// Check if the request has an error
		/// </summary>
		public bool hasError
	    {
	        get {
                AssertRequestValid();
                return isRequestError(nativeTaskHandle);
            }
	    }

        public static OpenGLAsyncReadbackRequest CreateTextureRequest(int textureOpenGLName, int mipmapLevel) {
            var result = new OpenGLAsyncReadbackRequest();
            result.nativeTaskHandle = RequestTextureMainThread(textureOpenGLName, mipmapLevel);
            GL.IssuePluginEvent(GetKickstartFunctionPtr(), result.nativeTaskHandle);
            return result;
        }
        public static OpenGLAsyncReadbackRequest CreateComputeBufferRequest(int computeBufferOpenGLName, int size) {
            var result = new OpenGLAsyncReadbackRequest();
            result.nativeTaskHandle = RequestComputeBufferMainThread(computeBufferOpenGLName, size);
            GL.IssuePluginEvent(GetKickstartFunctionPtr(), result.nativeTaskHandle);
            return result;
        }

        public bool Valid() {
            return RequestExists(this.nativeTaskHandle);
        }

        private void AssertRequestValid() {
            if (!Valid()) {
                throw new UnityException("The request is not valid!");
            }
        }

        public unsafe NativeArray<T> GetRawData<T>() where T:struct
		{
            AssertRequestValid();
            if (!done) {
                throw new InvalidOperationException("The request is not done yet!");
            }
			// Get data from cpp plugin
			void* ptr = null;
			int length = 0;
			getData_mainThread(this.nativeTaskHandle, ref ptr, ref length);

            //Copy data from plugin native memory to unity-controlled native memory.
            var resultNativeArray = new NativeArray<T>(length / UnsafeUtility.SizeOf<T>(), Allocator.Temp);
            UnsafeUtility.MemMove(resultNativeArray.GetUnsafePtr(), ptr, length);
            //Though there exists an api named NativeArrayUnsafeUtility.ConvertExistingDataToNativeArray.
            //It's only for internal use. The document on docs.unity3d.com is a lie.
            
            return resultNativeArray;
		}

		public void Update(bool force = false)
		{
            AssertRequestValid();
            GL.IssuePluginEvent(getfunction_update_renderThread(), this.nativeTaskHandle);
		}

		/// <summary>
		/// Has to be called to free the allocated buffer after it has been used
		/// </summary>
		public void Dispose() {
            AssertRequestValid();
            dispose(this.nativeTaskHandle);
        }

		[DllImport ("AsyncGPUReadbackPlugin")]
		private static extern bool isCompatible();
        [DllImport("AsyncGPUReadbackPlugin")]
        private static extern int RequestTextureMainThread(int texture, int miplevel);
        [DllImport("AsyncGPUReadbackPlugin")]
        private static extern int RequestComputeBufferMainThread(int bufferID, int bufferSize);
        [DllImport ("AsyncGPUReadbackPlugin")]
		private static extern IntPtr GetKickstartFunctionPtr();
		[DllImport ("AsyncGPUReadbackPlugin")]
		private static extern IntPtr getfunction_update_renderThread();
		[DllImport ("AsyncGPUReadbackPlugin")]
		private static extern unsafe void getData_mainThread(int event_id, ref void* buffer, ref int length);
        [DllImport("AsyncGPUReadbackPlugin")]
        private static extern bool isRequestError(int event_id);
        [DllImport("AsyncGPUReadbackPlugin")]
        private static extern bool RequestExists(int event_id);
        [DllImport ("AsyncGPUReadbackPlugin")]
		private static extern bool isRequestDone(int event_id);
		[DllImport ("AsyncGPUReadbackPlugin")]
		private static extern void dispose(int event_id);
	}
}