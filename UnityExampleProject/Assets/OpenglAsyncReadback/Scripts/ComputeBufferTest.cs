using System.Collections;
using System.Collections.Generic;
using UnityEngine;
using Unity.Collections;

namespace Yangrc.OpenGLAsyncReadback {
    public class ComputeBufferTest : MonoBehaviour {
        NativeArray<float> test;
        ComputeBuffer t;
        ComputeBufferAsyncGPUReadbackStarter starter;
        UniversalAsyncGPUReadbackRequest request;
        // Start is called before the first frame update
        IEnumerator Start() {
            t = new ComputeBuffer(100, 4, ComputeBufferType.Default);
            var tempList = new List<float>();
            for (int i = 0; i < 3; i++) {
                tempList.Add(i);
            }
            t.SetData(tempList);
            yield return null;
            starter = new ComputeBufferAsyncGPUReadbackStarter(t);
            request = starter.StartReadback();
            t.Dispose();
        }

        private void Update() {
            if (request.valid) {
                request.Update();
                if (request.done) {
                    test = request.GetData<float>();
                    foreach (var item in test) {
                        Debug.Log(item);
                    }
                    request.Dispose();
                }
            }
        }
    }
}