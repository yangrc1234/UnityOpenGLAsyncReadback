using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.Rendering;

namespace Yangrc.OpenGLAsyncReadback {

    /*
     * Directly using UniversalAsyncGPUReadbackRequest.Request causes render thread to sync due to GetNativeXXXPtr().
     * To avoid the cost, here we use a "starter" class.
     * A starter is responsible for all readback from a specified texture/compute buffer.
     * 
     * It gets native ptr at constructor, to avoid furthur cost.
    */

    public abstract class AsyncGPUReadbackStarter {
        protected static bool opengl {
            get {
                return SystemInfo.graphicsDeviceType == GraphicsDeviceType.OpenGLCore;
            }
        }

        public abstract UniversalAsyncGPUReadbackRequest StartReadback();
    }

    public class ComputeBufferAsyncGPUReadbackStarter : AsyncGPUReadbackStarter {

        private int ptr;
        private ComputeBuffer computeBuffer;
        public ComputeBufferAsyncGPUReadbackStarter(ComputeBuffer computeBuffer) {
            this.computeBuffer = computeBuffer;
            if (opengl)
                this.ptr = (int)computeBuffer.GetNativeBufferPtr();
        }

        public override UniversalAsyncGPUReadbackRequest StartReadback() {
            if (opengl)
                return UniversalAsyncGPUReadbackRequest.OpenGLRequestComputeBuffer(this.ptr, computeBuffer.stride * computeBuffer.count);
            else
                return UniversalAsyncGPUReadbackRequest.Request(computeBuffer);
        }
    }

    public class TextureAsyncGPUReadbackStarter : AsyncGPUReadbackStarter {

        private int ptr;
        public Texture texture { get; private set; }
        private int mipmapIndex;

        public TextureAsyncGPUReadbackStarter(Texture texture, int mipmapIndex = 0) {
            this.texture = texture;
            this.mipmapIndex = mipmapIndex;
            if (opengl)
                this.ptr = (int)this.texture.GetNativeTexturePtr();
        }

        public override UniversalAsyncGPUReadbackRequest StartReadback() {
            if (opengl)
                return UniversalAsyncGPUReadbackRequest.OpenGLRequestTexture(ptr, mipmapIndex);
            else
                return UniversalAsyncGPUReadbackRequest.Request(texture);
        }
    }
}