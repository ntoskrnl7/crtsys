#ifndef _CRTSYS_UNKNWN_
#define _CRTSYS_UNKNWN_

#pragma once

#include <Ldk/combaseapi.h>

#ifdef __cplusplus
struct IUnknown {
  virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
                                                  void **ppvObject) = 0;
  virtual ULONG STDMETHODCALLTYPE AddRef() = 0;
  virtual ULONG STDMETHODCALLTYPE Release() = 0;
};
#endif

#endif // _CRTSYS_UNKNWN_
