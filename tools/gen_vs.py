#!/usr/bin/env python3
"""Generates the Visual Studio solution for Counter-Strike: HvH.

    python tools/gen_vs.py

writes (repository root):
    CounterStrikeHvH.slnx        - new XML solution (VS 2022 17.13+ / VS 2026)
    CounterStrikeHvH.sln         - classic solution (VS 2019 / 2022 / 2026)
    msvc/cshvh.vcxproj(.filters) - the game
    msvc/raylib.vcxproj          - raylib 6.2 static library from third_party/raylib

Re-run it after adding or removing .cpp/.hpp files.
"""
import glob
import os
import uuid

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
NS = uuid.UUID('6b1d3f52-6f0e-4b8c-9a55-63736876682d')


def guid(name):
    return '{' + str(uuid.uuid5(NS, name)).upper() + '}'


GAME_GUID, RAYLIB_GUID = guid('cshvh'), guid('raylib')
VCXPROJ_TYPE = '{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}'
CONFIGS = ['Debug', 'Release']


def write(rel, text, bom=False):
    path = os.path.join(ROOT, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    data = text.replace('\r\n', '\n').replace('\n', '\r\n').encode('utf-8')
    with open(path, 'wb') as f:
        f.write((b'\xef\xbb\xbf' if bom else b'') + data)
    print('wrote', rel)


def winpath(p):
    return p.replace('/', '\\')


def project_configs():
    return ''.join(f'''    <ProjectConfiguration Include="{c}|x64">
      <Configuration>{c}</Configuration>
      <Platform>x64</Platform>
    </ProjectConfiguration>
''' for c in CONFIGS)


HEADER = '''<?xml version="1.0" encoding="utf-8"?>
<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
'''


def common_props(name, guid_, config_type):
    return f'''  <ItemGroup Label="ProjectConfigurations">
{project_configs()}  </ItemGroup>
  <PropertyGroup Label="Globals">
    <VCProjectVersion>17.0</VCProjectVersion>
    <ProjectGuid>{guid_}</ProjectGuid>
    <RootNamespace>{name}</RootNamespace>
    <ProjectName>{name}</ProjectName>
    <Keyword>Win32Proj</Keyword>
    <WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion>
  </PropertyGroup>
  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.Default.props" />
  <PropertyGroup Label="Configuration">
    <ConfigurationType>{config_type}</ConfigurationType>
    <!-- whatever toolset the installed Visual Studio has (v142 / v143 / v145) - no retargeting needed -->
    <PlatformToolset>$(DefaultPlatformToolset)</PlatformToolset>
    <CharacterSet>MultiByte</CharacterSet>
  </PropertyGroup>
  <PropertyGroup Condition="'$(Configuration)'=='Debug'" Label="Configuration">
    <UseDebugLibraries>true</UseDebugLibraries>
  </PropertyGroup>
  <PropertyGroup Condition="'$(Configuration)'=='Release'" Label="Configuration">
    <UseDebugLibraries>false</UseDebugLibraries>
  </PropertyGroup>
  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.props" />
  <ImportGroup Label="PropertySheets">
    <Import Project="$(UserRootDir)\\Microsoft.Cpp.$(Platform).user.props" Condition="exists('$(UserRootDir)\\Microsoft.Cpp.$(Platform).user.props')" Label="LocalAppDataPlatform" />
  </ImportGroup>
'''


def raylib_project():
    srcs = ['rcore', 'rshapes', 'rtextures', 'rtext', 'rmodels', 'raudio', 'rglfw']
    items = ''.join(f'    <ClCompile Include="$(RaylibSrc){s}.c" />\n' for s in srcs)
    hdrs = ''.join(f'    <ClInclude Include="$(RaylibSrc){h}" />\n' for h in ['raylib.h', 'raymath.h', 'rlgl.h', 'config.h'])
    return HEADER + common_props('raylib', RAYLIB_GUID, 'StaticLibrary') + f'''  <PropertyGroup>
    <RepoRoot>$([System.IO.Path]::GetFullPath('$(MSBuildThisFileDirectory)..\\'))</RepoRoot>
    <RaylibSrc>$(RepoRoot)third_party\\raylib\\src\\</RaylibSrc>
    <OutDir>$(RepoRoot)build\\vs\\lib\\$(Configuration)\\</OutDir>
    <IntDir>$(RepoRoot)build\\vs\\raylib\\$(Configuration)\\</IntDir>
    <TargetName>raylib</TargetName>
  </PropertyGroup>
  <ItemDefinitionGroup>
    <ClCompile>
      <WarningLevel>TurnOffAllWarnings</WarningLevel>
      <CompileAs>CompileAsC</CompileAs>
      <MultiProcessorCompilation>true</MultiProcessorCompilation>
      <AdditionalIncludeDirectories>$(RaylibSrc);$(RaylibSrc)external\\glfw\\include;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
      <PreprocessorDefinitions>_CRT_SECURE_NO_WARNINGS;_CRT_SECURE_NO_DEPRECATE;_CRT_NONSTDC_NO_DEPRECATE;WIN32;_LIB;PLATFORM_DESKTOP;GRAPHICS_API_OPENGL_33;%(PreprocessorDefinitions)</PreprocessorDefinitions>
    </ClCompile>
  </ItemDefinitionGroup>
  <ItemDefinitionGroup Condition="'$(Configuration)'=='Debug'">
    <ClCompile>
      <Optimization>Disabled</Optimization>
      <RuntimeLibrary>MultiThreadedDebug</RuntimeLibrary>
      <PreprocessorDefinitions>_DEBUG;%(PreprocessorDefinitions)</PreprocessorDefinitions>
    </ClCompile>
  </ItemDefinitionGroup>
  <ItemDefinitionGroup Condition="'$(Configuration)'=='Release'">
    <ClCompile>
      <Optimization>MaxSpeed</Optimization>
      <FunctionLevelLinking>true</FunctionLevelLinking>
      <IntrinsicFunctions>true</IntrinsicFunctions>
      <RuntimeLibrary>MultiThreaded</RuntimeLibrary>
      <PreprocessorDefinitions>NDEBUG;%(PreprocessorDefinitions)</PreprocessorDefinitions>
    </ClCompile>
  </ItemDefinitionGroup>
  <ItemGroup>
{items}  </ItemGroup>
  <ItemGroup>
{hdrs}  </ItemGroup>
  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.targets" />
</Project>
'''


def game_files():
    os.chdir(ROOT)
    cpp = sorted(glob.glob('src/**/*.cpp', recursive=True))
    hpp = sorted(glob.glob('src/**/*.hpp', recursive=True))
    return cpp, hpp


def game_project(cpp, hpp):
    items = ''.join(f'    <ClCompile Include="..\\{winpath(f)}" />\n' for f in cpp)
    hdrs = ''.join(f'    <ClInclude Include="..\\{winpath(f)}" />\n' for f in hpp)
    docs = ''.join(f'    <None Include="..\\{f}" />\n' for f in ['README.md', 'CREDITS.md'])
    warn_off = '4244;4305;4267;4018;4996;4100;4101;4189;4456;4457;4458;4459;4146;4838;4309;4127;4702;4065'
    return HEADER + common_props('cshvh', GAME_GUID, 'Application') + f'''  <PropertyGroup>
    <RepoRoot>$([System.IO.Path]::GetFullPath('$(MSBuildThisFileDirectory)..\\'))</RepoRoot>
    <OutDir>$(RepoRoot)bin\\</OutDir>
    <IntDir>$(RepoRoot)build\\vs\\cshvh\\$(Configuration)\\</IntDir>
    <TargetName Condition="'$(Configuration)'=='Debug'">cshvh_debug</TargetName>
    <TargetName Condition="'$(Configuration)'=='Release'">cshvh</TargetName>
    <!-- F5 runs the game from the repository root, where assets/ and cfg/ live -->
    <LocalDebuggerWorkingDirectory>$(RepoRoot)</LocalDebuggerWorkingDirectory>
    <DebuggerFlavor>WindowsLocalDebugger</DebuggerFlavor>
  </PropertyGroup>
  <ItemDefinitionGroup>
    <ClCompile>
      <WarningLevel>Level3</WarningLevel>
      <SDLCheck>false</SDLCheck>
      <ConformanceMode>true</ConformanceMode>
      <LanguageStandard>stdcpp17</LanguageStandard>
      <MultiProcessorCompilation>true</MultiProcessorCompilation>
      <AdditionalIncludeDirectories>$(RepoRoot)third_party\\raylib\\src;$(RepoRoot)src;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
      <PreprocessorDefinitions>_USE_MATH_DEFINES;NOMINMAX;WIN32_LEAN_AND_MEAN;_CRT_SECURE_NO_WARNINGS;_CRT_NONSTDC_NO_DEPRECATE;PLATFORM_DESKTOP_GLFW;%(PreprocessorDefinitions)</PreprocessorDefinitions>
      <AdditionalOptions>/utf-8 /bigobj /Zc:__cplusplus %(AdditionalOptions)</AdditionalOptions>
      <DisableSpecificWarnings>{warn_off};%(DisableSpecificWarnings)</DisableSpecificWarnings>
    </ClCompile>
    <Link>
      <AdditionalDependencies>opengl32.lib;gdi32.lib;winmm.lib;shell32.lib;user32.lib;winhttp.lib;%(AdditionalDependencies)</AdditionalDependencies>
      <GenerateDebugInformation>true</GenerateDebugInformation>
      <!-- 8 MB stack like on Linux (worker threads inherit it) -->
      <StackReserveSize>8388608</StackReserveSize>
    </Link>
  </ItemDefinitionGroup>
  <ItemDefinitionGroup Condition="'$(Configuration)'=='Debug'">
    <ClCompile>
      <Optimization>Disabled</Optimization>
      <RuntimeLibrary>MultiThreadedDebug</RuntimeLibrary>
      <PreprocessorDefinitions>_DEBUG;%(PreprocessorDefinitions)</PreprocessorDefinitions>
    </ClCompile>
    <Link>
      <!-- Debug keeps a console window with the game log -->
      <SubSystem>Console</SubSystem>
    </Link>
  </ItemDefinitionGroup>
  <ItemDefinitionGroup Condition="'$(Configuration)'=='Release'">
    <ClCompile>
      <Optimization>MaxSpeed</Optimization>
      <FunctionLevelLinking>true</FunctionLevelLinking>
      <IntrinsicFunctions>true</IntrinsicFunctions>
      <RuntimeLibrary>MultiThreaded</RuntimeLibrary>
      <PreprocessorDefinitions>NDEBUG;%(PreprocessorDefinitions)</PreprocessorDefinitions>
    </ClCompile>
    <Link>
      <SubSystem>Windows</SubSystem>
      <EntryPointSymbol>mainCRTStartup</EntryPointSymbol>
      <EnableCOMDATFolding>true</EnableCOMDATFolding>
      <OptimizeReferences>true</OptimizeReferences>
    </Link>
  </ItemDefinitionGroup>
  <ItemGroup>
{items}  </ItemGroup>
  <ItemGroup>
{hdrs}  </ItemGroup>
  <ItemGroup>
{docs}  </ItemGroup>
  <ItemGroup>
    <ResourceCompile Include="..\\res\\cshvh.rc" />
  </ItemGroup>
  <ItemGroup>
    <ProjectReference Include="raylib.vcxproj">
      <Project>{RAYLIB_GUID}</Project>
    </ProjectReference>
  </ItemGroup>
  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.targets" />
</Project>
'''


def game_filters(cpp, hpp):
    dirs = set()
    for f in cpp + hpp:
        d = os.path.dirname(f)
        while d:
            dirs.add(d)
            d = os.path.dirname(d)
    flt = ''.join(f'''    <Filter Include="{winpath(d)}">
      <UniqueIdentifier>{guid('filter:' + d)}</UniqueIdentifier>
    </Filter>
''' for d in sorted(dirs))
    def entries(tag, files):
        return ''.join(f'''    <{tag} Include="..\\{winpath(f)}">
      <Filter>{winpath(os.path.dirname(f))}</Filter>
    </{tag}>
''' for f in files)
    return f'''<?xml version="1.0" encoding="utf-8"?>
<Project ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup>
{flt}  </ItemGroup>
  <ItemGroup>
{entries('ClCompile', cpp)}  </ItemGroup>
  <ItemGroup>
{entries('ClInclude', hpp)}  </ItemGroup>
</Project>
'''


def sln():
    projs = [('cshvh', 'msvc\\cshvh.vcxproj', GAME_GUID), ('raylib', 'msvc\\raylib.vcxproj', RAYLIB_GUID)]
    out = ['', 'Microsoft Visual Studio Solution File, Format Version 12.00', '# Visual Studio Version 17',
           'VisualStudioVersion = 17.0.31903.59', 'MinimumVisualStudioVersion = 10.0.40219.1']
    for name, path, g in projs:   # first project = default startup project
        out += [f'Project("{VCXPROJ_TYPE}") = "{name}", "{path}", "{g}"', 'EndProject']
    out += ['Global', '\tGlobalSection(SolutionConfigurationPlatforms) = preSolution']
    out += [f'\t\t{c}|x64 = {c}|x64' for c in CONFIGS]
    out += ['\tEndGlobalSection', '\tGlobalSection(ProjectConfigurationPlatforms) = postSolution']
    for _, _, g in projs:
        for c in CONFIGS:
            out += [f'\t\t{g}.{c}|x64.ActiveCfg = {c}|x64', f'\t\t{g}.{c}|x64.Build.0 = {c}|x64']
    out += ['\tEndGlobalSection', '\tGlobalSection(SolutionProperties) = preSolution', '\t\tHideSolutionNode = FALSE',
            '\tEndGlobalSection', '\tGlobalSection(ExtensibilityGlobals) = postSolution',
            f'\t\tSolutionGuid = {guid("solution")}', '\tEndGlobalSection', 'EndGlobal', '']
    return '\n'.join(out)


def slnx():
    return '''<Solution>
  <Configurations>
    <BuildType Name="Debug" />
    <BuildType Name="Release" />
    <Platform Name="x64" />
  </Configurations>
  <Project Path="msvc/cshvh.vcxproj" />
  <Folder Name="/third_party/">
    <Project Path="msvc/raylib.vcxproj" />
  </Folder>
  <Folder Name="/Solution Items/">
    <File Path="README.md" />
    <File Path="CREDITS.md" />
    <File Path="build.sh" />
    <File Path="CMakeLists.txt" />
  </Folder>
</Solution>
'''


if __name__ == '__main__':
    cpp, hpp = game_files()
    write('msvc/raylib.vcxproj', raylib_project())
    write('msvc/cshvh.vcxproj', game_project(cpp, hpp))
    write('msvc/cshvh.vcxproj.filters', game_filters(cpp, hpp))
    write('CounterStrikeHvH.sln', sln(), bom=True)
    write('CounterStrikeHvH.slnx', slnx())
