// Copyright (C) 2003 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

#include <tchar.h>
#include <windows.h>
#include <d3dx9.h>

#include "Common.h"
#include "Thread.h"
#include "LogManager.h"


#if defined(HAVE_WX) && HAVE_WX
#include "Debugger/Debugger.h"
GFXDebuggerDX9 *m_DebuggerFrame = NULL;
#endif // HAVE_WX

#include "svnrev.h"
#include "resource.h"
#include "main.h"
#include "Config.h"
#include "Fifo.h"
#include "OpcodeDecoding.h"
#include "TextureCache.h"
#include "BPStructs.h"
#include "VertexManager.h"
#include "VertexLoaderManager.h"
#include "VertexShaderManager.h"
#include "PixelShaderManager.h"
#include "VertexShaderCache.h"
#include "PixelShaderCache.h"
#include "DlgSettings.h"
#include "D3DPostprocess.h"
#include "D3DTexture.h"
#include "D3DUtil.h"
#include "W32Util/Misc.h"
#include "EmuWindow.h"
#include "VideoState.h"
#include "XFBConvert.h"

#include "Utils.h"

HINSTANCE g_hInstance = NULL;
SVideoInitialize g_VideoInitialize;
PLUGIN_GLOBALS* globals = NULL;
int initCount = 0;

static volatile u32 s_AccessEFBResult = 0, s_EFBx, s_EFBy;
static volatile EFBAccessType s_AccessEFBType;
static Common::Event s_AccessEFBDone;
static Common::CriticalSection s_criticalEFB;

bool HandleDisplayList(u32 address, u32 size)
{
	return false;
}

// This is used for the functions right below here which use wxwidgets
#if defined(HAVE_WX) && HAVE_WX
#ifdef _WIN32
	WXDLLIMPEXP_BASE void wxSetInstance(HINSTANCE hInst);
#endif

wxWindow* GetParentedWxWindow(HWND Parent)
{
#ifdef _WIN32
	wxSetInstance((HINSTANCE)g_hInstance);
#endif
	wxWindow *win = new wxWindow();
#ifdef _WIN32
	win->SetHWND((WXHWND)Parent);
	win->AdoptAttributesFromHWND();
#endif
	return win;
}
#endif

#if defined(HAVE_WX) && HAVE_WX
void DllDebugger(HWND _hParent, bool Show)
{
    //SetWindowTextA(EmuWindow::GetWnd(), "Hello");

	if (!m_DebuggerFrame)
		m_DebuggerFrame = new GFXDebuggerDX9(GetParentedWxWindow(_hParent));

	if (Show)
		m_DebuggerFrame->Show();
	else
		m_DebuggerFrame->Hide();
}
#else
void DllDebugger(HWND _hParent, bool Show) { }
#endif



#if defined(HAVE_WX) && HAVE_WX
	class wxDLLApp : public wxApp
	{
		bool OnInit()
		{
			return true;
		}
	};
	IMPLEMENT_APP_NO_MAIN(wxDLLApp) 
	WXDLLIMPEXP_BASE void wxSetInstance(HINSTANCE hInst);
#endif



BOOL APIENTRY DllMain(	HINSTANCE hinstDLL,	// DLL module handle
						DWORD dwReason,		// reason called
						LPVOID lpvReserved)	// reserved
{
	switch (dwReason)
	{
	case DLL_PROCESS_ATTACH:
		{
			#if defined(HAVE_WX) && HAVE_WX
			// Use wxInitialize() if you don't want GUI instead of the following 12 lines
			wxSetInstance((HINSTANCE)hinstDLL);
			int argc = 0;
			char **argv = NULL;
			wxEntryStart(argc, argv);
			if (!wxTheApp || !wxTheApp->CallOnInit())
				return FALSE;
			#endif
		}
		break;
	case DLL_PROCESS_DETACH:
		#if defined(HAVE_WX) && HAVE_WX
			// This causes a "stop hang", if the gfx config dialog has been opened.
			// Old comment: "Use wxUninitialize() if you don't want GUI"
			wxEntryCleanup();
		#endif
		break;
	default:
		break;
	}

	g_hInstance = hinstDLL;
	return TRUE;
}

unsigned int Callback_PeekMessages()
{
	//TODO: peek message
	MSG msg;
	while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
	{
		if (msg.message == WM_QUIT)
			return FALSE;
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	return TRUE;
}


void UpdateFPSDisplay(const char *text)
{
	char temp[512];
	sprintf_s(temp, 512, "SVN R%i: DX9: %s", SVN_REV, text);
    SetWindowTextA(EmuWindow::GetWnd(), temp);
}


bool Init()
{
	g_Config.Load();

	if (initCount == 0)
	{
        // create the window
        if (!g_Config.renderToMainframe || g_VideoInitialize.pWindowHandle == NULL) // ignore parent for this plugin
        {
            g_VideoInitialize.pWindowHandle = (void*)EmuWindow::Create(NULL, g_hInstance, _T("Loading - Please wait."));
        }
		else
		{
			g_VideoInitialize.pWindowHandle = (void*)EmuWindow::Create((HWND)g_VideoInitialize.pWindowHandle, g_hInstance, _T("Loading - Please wait."));
		}

		if (g_VideoInitialize.pWindowHandle == NULL)
		{
			MessageBox(GetActiveWindow(), _T("An error has occurred while trying to create the window."), _T("Fatal Error"), MB_OK);
			return false;
		}

		EmuWindow::Show();
		g_VideoInitialize.pPeekMessages = Callback_PeekMessages;
		g_VideoInitialize.pUpdateFPSDisplay = UpdateFPSDisplay;

		if (FAILED(D3D::Init()))
		{
			MessageBox(GetActiveWindow(), _T("Unable to initialize Direct3D. Please make sure that you have DirectX 9.0c correctly installed."), _T("Fatal Error"), MB_OK);
			return false;
		}
		InitXFBConvTables();
	}
	initCount++;

	return true;
}

void DeInit()
{
	initCount--;
	if (initCount == 0)
	{
		D3D::Shutdown();
        EmuWindow::Close();
	}
}

void GetDllInfo (PLUGIN_INFO* _PluginInfo)
{
	_PluginInfo->Version = 0x0100;
	_PluginInfo->Type = PLUGIN_TYPE_VIDEO;
#ifdef DEBUGFAST
	sprintf_s(_PluginInfo->Name, 100, "Dolphin Direct3D9 (DebugFast)");
#else
#ifndef _DEBUG
	sprintf_s(_PluginInfo->Name, 100, "Dolphin Direct3D9");
#else
	sprintf_s(_PluginInfo->Name, 100, "Dolphin Direct3D9 (Debug)");
#endif
#endif
}

void SetDllGlobals(PLUGIN_GLOBALS* _pPluginGlobals) {
	globals = _pPluginGlobals;
	LogManager::SetInstance((LogManager *)globals->logManager);
}

void DllAbout(HWND _hParent)
{
	DialogBoxA(g_hInstance,(LPCSTR)IDD_ABOUT,_hParent,(DLGPROC)AboutProc);
}

void DllConfig(HWND _hParent)
{
	if (Init())
	{
		DlgSettings_Show(g_hInstance,_hParent);
		DeInit();
	}
}

void Initialize(void *init)
{

    SVideoInitialize *_pVideoInitialize = (SVideoInitialize*)init;
	frameCount = 0;
	g_VideoInitialize = *_pVideoInitialize;
	Init();
    _pVideoInitialize->pPeekMessages = g_VideoInitialize.pPeekMessages;
    _pVideoInitialize->pUpdateFPSDisplay = g_VideoInitialize.pUpdateFPSDisplay;
    _pVideoInitialize->pWindowHandle = g_VideoInitialize.pWindowHandle;

	Renderer::AddMessage("Dolphin Direct3D9 Video Plugin.",5000);

}

void DoState(unsigned char **ptr, int mode) {
	// Clear all caches
	TextureCache::Invalidate(false);

	PointerWrap p(ptr, mode);
	VideoCommon_DoState(p);
	//PanicAlert("Saving/Loading state from DirectX9");
}

void Video_EnterLoop()
{
	Fifo_EnterLoop(g_VideoInitialize);
}

void Video_ExitLoop()
{
	Fifo_ExitLoop();
}

void Video_SetRendering(bool bEnabled) {
	Fifo_SetRendering(bEnabled);
}

void Video_Prepare(void)
{
	Renderer::Init(g_VideoInitialize);

	TextureCache::Init();

	BPInit();
	VertexManager::Init();
	Fifo_Init();
	VertexLoaderManager::Init();
	OpcodeDecoder_Init();
	VertexShaderCache::Init();
	VertexShaderManager::Init();
	PixelShaderCache::Init();
	PixelShaderManager::Init();
}

void Shutdown(void)
{
	Fifo_Shutdown();
	OpcodeDecoder_Shutdown();
	VertexManager::Shutdown();
	VertexShaderManager::Shutdown();
	VertexLoaderManager::Shutdown();
	VertexShaderCache::Shutdown();
	PixelShaderCache::Shutdown();
	PixelShaderManager::Shutdown();
	TextureCache::Shutdown();
	Renderer::Shutdown();
	DeInit();
}

void Video_SendFifoData(u8* _uData, u32 len)
{
	Fifo_SendFifoData(_uData, len);
}

void VideoFifo_CheckSwapRequest()
{
	// CPU swap unimplemented
}

void VideoFifo_CheckSwapRequestAt(u32 xfbAddr, u32 fbWidth, u32 fbHeight)
{
	// CPU swap unimplemented
}

void Video_BeginField(u32 xfbAddr, FieldType field, u32 fbWidth, u32 fbHeight)
{
	/*
	ConvertXFB(tempBuffer, _pXFB, _dwWidth, _dwHeight);

	// blubb
	static LPDIRECT3DTEXTURE9 pTexture = D3D::CreateTexture2D((BYTE*)tempBuffer, _dwWidth, _dwHeight, _dwWidth, D3DFMT_A8R8G8B8);

	D3D::ReplaceTexture2D(pTexture, (BYTE*)tempBuffer, _dwWidth, _dwHeight, _dwWidth, D3DFMT_A8R8G8B8);
	D3D::dev->SetTexture(0, pTexture);

	D3D::quad2d(0,0,(float)Postprocess::GetWidth(),(float)Postprocess::GetHeight(), 0xFFFFFFFF);

	D3D::EndFrame();
	D3D::BeginFrame();*/
}

void Video_EndField()
{
}

void Video_AddMessage(const char* pstr, u32 milliseconds)
{
	Renderer::AddMessage(pstr,milliseconds);
}

HRESULT ScreenShot(const char *File)
{
	if (D3D::dev == NULL)
		return S_FALSE;

	D3DDISPLAYMODE DisplayMode;
	if (FAILED(D3D::dev->GetDisplayMode(0, &DisplayMode)))
		return S_FALSE;

	LPDIRECT3DSURFACE9 surf;
	if (FAILED(D3D::dev->CreateOffscreenPlainSurface(DisplayMode.Width, DisplayMode.Height, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &surf, NULL)))
		return S_FALSE;

	if (FAILED(D3D::dev->GetFrontBufferData(0, surf)))
	{
		surf->Release();
		return S_FALSE;
	}

	RECT rect;
    ::GetWindowRect(EmuWindow::GetWnd(), &rect);
	if (FAILED(D3DXSaveSurfaceToFileA(File, D3DXIFF_PNG, surf, NULL, &rect)))
	{
		surf->Release();
		return S_FALSE;
	}

	surf->Release();
	return S_OK;
}

void Video_Screenshot(const char *_szFilename)
{
	if (ScreenShot(_szFilename) != S_OK)
		PanicAlert("Error while capturing screen");
	else {
		std::string message =  "Saved ";
		message += _szFilename;
		Renderer::AddMessage(message, 2000);
	}
}

void VideoFifo_CheckEFBAccess()
{
	s_criticalEFB.Enter();
	s_AccessEFBResult = 0;

	/*
	switch (s_AccessEFBType)
	{
	case PEEK_Z:
		break;

	case POKE_Z:
		break;

	case PEEK_COLOR:
		break;

	case POKE_COLOR:
		break;

	default:
		break;
	}
	*/

	if (g_VideoInitialize.bUseDualCore)
		s_AccessEFBDone.Set();

	s_criticalEFB.Leave();
}

u32 Video_AccessEFB(EFBAccessType type, u32 x, u32 y)
{
	u32 result;

s_criticalEFB.Enter();

	s_AccessEFBType = type;
	s_EFBx = x;
	s_EFBy = y;

	if (g_VideoInitialize.bUseDualCore)
		s_AccessEFBDone.Init();

s_criticalEFB.Leave();

	if (g_VideoInitialize.bUseDualCore)
		s_AccessEFBDone.Wait();
	else
		VideoFifo_CheckEFBAccess();

s_criticalEFB.Enter();

	if (g_VideoInitialize.bUseDualCore)
		s_AccessEFBDone.Shutdown();

	result = s_AccessEFBResult;

s_criticalEFB.Leave();

	return result;
}
