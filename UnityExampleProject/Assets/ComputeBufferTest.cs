using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class ComputeBufferTest : MonoBehaviour
{
    ComputeBuffer t;
    AsyncGPUReadbackPluginNs.AsyncGPUReadbackPluginRequest request;
    // Start is called before the first frame update
    IEnumerator Start()
    {
        t = new ComputeBuffer(100, 4, ComputeBufferType.Default);
        var tempList = new List<float>();
        for (int i = 0; i < 100; i++) {
            tempList.Add(i);
        }
        t.SetData(tempList);
        yield return null;
        request = AsyncGPUReadbackPluginNs.AsyncGPUReadbackPlugin.Request(t);
    }

    private void Update() {
        if (request != null) {
            request.Update();
            if (request.done) {
                var rawData = request.GetRawData();
                for (int i = 0; i < 100; i++) {
                    Debug.Log(System.BitConverter.ToSingle(rawData, i * 4));
                }
                request.Dispose();
                request = null;
            }
        }
    }
}
