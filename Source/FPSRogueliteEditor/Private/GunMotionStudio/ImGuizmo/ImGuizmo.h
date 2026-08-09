// NOT part of the vendored ImGuizmo distribution — a build-integration shim only. The pristine, byte-for-byte
// upstream ImGuizmo.h (v1.10 tag, https://github.com/CedricGuillemet/ImGuizmo, MIT — see LICENSE in this folder)
// lives untouched at Vendor/ImGuizmo.h; this file exists purely so ImGuizmo.cpp's own unmodified
// `#include "ImGuizmo.h"` line (relative to its own directory) resolves to something that also fixes a real macro
// collision, without editing a single byte of the vendored source (GunMotionTool_Spec.md §1 "업스트림 원본 그대로,
// 수정 금지").
//
// The collision: "ImGui for Unreal Engine" (Plugins/ImGui) is a module literally named "ImGui", so UnrealBuildTool's
// auto-generated per-module DLL export/import macro for it is ALSO named IMGUI_API — the exact same identifier
// vanilla Dear ImGui and its addons (ImGuizmo included) have always used for THEIR OWN "build me as part of one
// shared ImGui DLL" convention (ImGuizmo.h: `#ifndef IMGUI_API #define IMGUI_API #endif`). Since UBT defines
// IMGUI_API as DLLIMPORT for every OTHER module in the target (correct for the real ImGui plugin's own exports, e.g.
// imgui_internal.h's `extern IMGUI_API ImGuiContext* GImGui;` — that really does live in a separate DLL and needs
// DLLIMPORT), ImGuizmo.h's guard silently no-ops (IMGUI_API is already defined) and every ImGuizmo::* function
// declaration ALSO gets decorated DLLIMPORT — even though ImGuizmo.cpp is compiled directly into THIS module
// (FPSRogueliteEditor), never a separate DLL of its own. MSVC then errors/warns-as-error C4273 ("dll linkage
// mismatch") the moment ImGuizmo.cpp provides a body for a DLLIMPORT-decorated declaration.
//
// The fix only needs to apply to ImGuizmo's OWN declarations, not to anything imgui.h/imgui_internal.h declared
// elsewhere — critically, it must NOT leak past this one include, because Unity builds concatenate multiple .cpp
// files into one translation unit: if this shim's redefinition were left in effect, a LATER file in the same Unity
// blob that first-includes imgui_internal.h (e.g. FPSRGunMotionStudioUI.cpp, included after this file's user
// re-uses IMGUI_API=empty) would get GImGui declared WITHOUT dllimport too — an actual cross-DLL data symbol that
// genuinely needs it, silently turning into an unresolved-external at link time (measured: this happened on the
// first build attempt of this exact setup before the push_macro/pop_macro pair below was added). #pragma
// push_macro/pop_macro is the standard, MSVC+Clang+GCC-portable way to save and restore a macro's definition across
// exactly this scope, regardless of Unity-build file ordering.
#pragma once

#pragma push_macro("IMGUI_API")
#undef IMGUI_API
#define IMGUI_API

#include "Vendor/ImGuizmo.h"

#pragma pop_macro("IMGUI_API")
