using System.Collections;
using System.Collections.Generic;
using UnityEngine;

namespace Yangrc.OpenGLAsyncReadback {
    /// <summary>
    /// A helper class to trigger readback update every frame.
    /// </summary>
    [AddComponentMenu("")]
    public class AsyncReadbackUpdater : MonoBehaviour {
        public static AsyncReadbackUpdater instance;
        private void Awake() {
            instance = this;
        }
        void Update() {
            OpenGLAsyncReadbackRequest.Update();
            RenderTextureRegistery.ClearDeadRefs();
        }
    }
}