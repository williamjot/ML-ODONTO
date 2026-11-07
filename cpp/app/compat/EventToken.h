// Substituto do EventToken.h do Windows SDK, que falta no MinGW (compilação com Zig/clang).
// O WebView2.h só precisa desta estrutura.
#pragma once
#ifndef __eventtoken_h__
#define __eventtoken_h__
typedef struct EventRegistrationToken {
    __int64 value;
} EventRegistrationToken;
#endif
