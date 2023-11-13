-- Function to create a directory if it doesn't exist
function createDirectoryIfNeeded(directory)
    if not os.isdir(directory) then
        os.mkdir(directory)
    end
end

workspace "Gluttony"
	platforms "x64"

	configurations
	{
		"Debug",
		"Release",
		"Dist"
	}

outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

project "Gluttony"
	location "Gluttony"
	kind "SharedLib"
	language "C++"
	
	-- Call the function to create the output directory
	createDirectoryIfNeeded("../bin/" .. outputdir .. "/Sandbox")

	targetdir ("bin/" .. outputdir .. "/%{prj.name}")
	objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

	files
    {
		"%{prj.name}/src/**.h",
		"%{prj.name}/src/**.cpp"
	}

	includedirs
	{
		"%{prj.name}/src"
	}

	filter "system:windows"
		cppdialect "C++17"
		staticruntime "On"
		systemversion "latest"

		defines
		{
			"GL_PLATFORM_WINDOWS",
			"GL_BUILD_DLL"
		}

		postbuildcommands
		{
			-- Call the function to create the output directory
			createDirectoryIfNeeded("../bin/" .. outputdir .. "/Sandbox")
			("{COPY} %{cfg.buildtarget.relpath} ../bin/" .. outputdir .. "/Sandbox")
		}

	filter "configurations:Debug"
		defines "GL_DEBUG"
		symbols "On"

	filter "configurations:Release"
		defines "GL_RELEASE"
		optimize "On"

	filter "configurations:Dist"
		defines "GL_DIST"
		optimize "On"

project "Sandbox"
	location "Sandbox"
	kind "ConsoleApp"
	language "C++"

	targetdir ("bin/" .. outputdir .. "/%{prj.name}")
	objdir ("bin-int/" .. outputdir .. "/%{prj.name}")

	files
	{
		"%{prj.name}/src/**.h",
		"%{prj.name}/src/**.cpp"
	}

	includedirs
	{
		"Gluttony/src"
	}

	links
	{
		"Gluttony"
	}

	filter "system:windows"
		cppdialect "C++17"
		staticruntime "On"
		systemversion "latest"

		defines
		{
			"GL_PLATFORM_WINDOWS"
		}

	filter "configurations:Debug"
		defines "GL_DEBUG"
		symbols "On"

	filter "configurations:Release"
		defines "GL_RELEASE"
		optimize "On"

	filter "configurations:Dist"
		defines "GL_DIST"
		optimize "On"