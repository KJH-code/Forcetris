import sys
import main
from cx_Freeze import setup, Executable

# Dependency list.
build_exe_options = {
    "packages": ["pygame"], 
    "excludes": ["tkinter", "numpy"],
    "include_files": ["textures", "music", "sound", "data", "screenshots"]
}
# Base for GUI apps on Windows.
base = "Win32GUI" if sys.platform == "win32" else None

setup(
    name = "Forcetris",
    version = main.__version__,
    description = main.__doc__,
    author = main.__author__,
    license = main.__license__,
    url = main.__url__,
    options = {"build_exe": build_exe_options},
    executables = [Executable("main.py", base=base)]
)
