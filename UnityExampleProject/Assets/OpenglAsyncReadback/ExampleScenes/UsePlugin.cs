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
        private void Start() {
            var cam = GetComponent<Camera>();
        }

        void Update() {
            while (_requests.Count > 0) {
                var req = _requests.Peek();

                if (req.hasError) {
                    Debug.LogError("GPU readback error detected.");
                    _requests.Dequeue();
                } else if (req.done) {
                    // Get data from the request when it's done
                    var buffer = req.GetData<byte>();

                    // Save the image
                    Camera cam = GetComponent<Camera>();
                    SaveBitmap(buffer, cam.pixelWidth, cam.pixelHeight);

                    _requests.Dequeue();
                } else {
                    break;
                }
            }
        }

        void OnRenderImage(RenderTexture source, RenderTexture destination) {
            Graphics.Blit(source, destination);
            
            if (Time.frameCount % 60 == 0) {
                if (_requests.Count < 8)
                    _requests.Enqueue(UniversalAsyncGPUReadbackRequest.Request(source));
                else
                    Debug.LogWarning("Too many requests.");
           }
        }

        void SaveBitmap(NativeArray<byte> buffer, int width, int height) {
            Debug.Log("Write to file");
            var texture = new Texture2D(width, height, TextureFormat.RGBAHalf, false);
            texture.LoadRawTextureData(buffer);
            File.WriteAllBytes("test.png", ImageConversion.EncodeToPNG(texture));
        }
    }
}