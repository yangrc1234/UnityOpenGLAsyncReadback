using UnityEngine;
using System.Collections;
using System.Collections.Generic;
using System;
using System.Runtime.InteropServices;
using Unity.Collections;
using System.IO;
using Yangrc.OpenGLAsyncReadback;

/// <summary>
/// Exemple of usage inspirated from https://github.com/keijiro/AsyncCaptureTest/blob/master/Assets/AsyncCapture.cs
/// </summary>
/// 
namespace Yangrc.OpenGLAsyncReadback {
    public class UsePlugin : MonoBehaviour {

        Queue<UniversalAsyncGPUReadbackRequest> _requests = new Queue<UniversalAsyncGPUReadbackRequest>();

        private RenderTexture rt;
        private AsyncGPUReadbackStarter AsyncGPUReadbackStarter;
        private void Start() {
            var cam = GetComponent<Camera>();
            rt = new RenderTexture(1000, 500, 24, UnityEngine.RenderTextureFormat.DefaultHDR, RenderTextureReadWrite.sRGB);
            rt.Create();
            AsyncGPUReadbackStarter = new TextureAsyncGPUReadbackStarter(rt);
            GetComponent<Camera>().targetTexture = rt;
        }

        void Update() {
            while (_requests.Count > 0) {
                var req = _requests.Peek();

                // You need to explicitly ask for an update regularly
                req.Update();

                if (req.hasError) {
                    Debug.LogError("GPU readback error detected.");
                    req.Dispose();
                    _requests.Dequeue();
                } else if (req.done) {
                    // Get data from the request when it's done
                    var buffer = req.GetData<byte>();

                    // Save the image
                    Camera cam = GetComponent<Camera>();
                    SaveBitmap(buffer, cam.pixelWidth, cam.pixelHeight);

                    // You need to explicitly Dispose data after using them
                    req.Dispose();

                    _requests.Dequeue();
                } else {
                    break;
                }
            }
        }

        void OnRenderImage(RenderTexture source, RenderTexture destination) {
            Graphics.Blit(source, destination);
            
            //if (Time.frameCount % 60 == 0) {
                if (_requests.Count < 8)
                    _requests.Enqueue(AsyncGPUReadbackStarter.StartReadback());
                else
                    Debug.LogWarning("Too many requests.");
           //}
        }

        void SaveBitmap(NativeArray<byte> buffer, int width, int height) {
            Debug.Log("Write to file");
            for (int i = 0; i < 100; i++) {
                Debug.Log(buffer[i]);
            }
            File.WriteAllBytes("test.bin", buffer.ToArray());
        }
    }
}