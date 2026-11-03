#pragma once

#include "Application.h"

bool InitializeRenderer(HINSTANCE hInstance, HWND hWnd);
void ShutdownRenderer();
void Render();
void OnWindowResized(uint32_t width, uint32_t height);
void OnMouseDrag(float deltaX, float deltaY);
void OnMouseWheel(float steps);
