using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using Unity.Collections;

namespace Yangrc.OpenGLAsyncReadback {
    public class ComputeBufferTest : MonoBehaviour {
        NativeArray<float> test;
        ComputeBuffer t;
        UniversalAsyncGPUReadbackRequest request;
        // Start is called before the first frame update

        IEnumerator Start() {
            t = new ComputeBuffer(100, 4, ComputeBufferType.Default);
            var tempList = new List<float>();
            for (int i = 0; i < 100; i++) {
                tempList.Add(i);
            }
            t.SetData(tempList);
            yield return null;
            request = UniversalAsyncGPUReadbackRequest.Request(t);
            t.Dispose();
        }

        private void Update() {
            if (request.valid) {
                if (request.done) {
                    test = request.GetData<float>();
                    foreach (var item in test) {
                        Debug.Log(item);
                    }
                }
            }
        }
    }
}