#pragma once

#ifdef GL_PLATFORM_WINDOWS
	#ifdef GL_BUILD_DLL
		#define GLUTTONY_API __declspec(dllexport)
	#else
		#define GLUTTONY_API __declspec(dllimport)
	#endif // GL_BUILD_DLL
#else
	#error Gluttony only supports Windows
#endif // GL_PLATFORM_WINDOWS

#define BIT(x) (1 << x)
