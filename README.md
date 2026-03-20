# img2file

Paste clipboard images directly into Windows Explorer folders as PNG files.

Copy an image to your clipboard (screenshot, browser image, anything), open a folder in Windows Explorer, press Ctrl+V. The image is saved as a PNG file. No popups, no dialogs, no extra steps.

Windows has no built-in way to paste a clipboard image into a folder. If you take a screenshot or copy an image from a browser, Ctrl+V in Explorer just does nothing. img2file fixes that. It's a tiny, invisible background process that adds this missing feature to Windows Explorer and the Desktop.

## How it works

A tiny background process (40KB binary, ~150KB RAM) installs a low-level keyboard hook. When you press Ctrl+V and the active window is an Explorer folder or the Desktop, it checks the clipboard for image data. If there's an image, it saves it as a PNG. If there's no image (or the clipboard has files), the normal paste behavior happens as usual.

Files are named sequentially: `image_001.png`, `image_002.png`, `image_003.png`, etc.

The process has no window, no tray icon, and no UI of any kind. It only activates in Explorer and on the Desktop. Pasting in browsers, text editors, and all other applications is completely unaffected.

## Building

Requires CMake and a C++ compiler (MinGW or MSVC).

```
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Or with MSVC:

```
cmake -B build
cmake --build build --config Release
```

The output is `build/img2file.exe`.

## Installing

Run `install.bat`. This does three things:

1. Copies the exe to `%LOCALAPPDATA%\img2file\`
2. Creates a scheduled task that starts it automatically on login
3. Starts it immediately

To remove it, run `uninstall.bat`.

## How it handles edge cases

- **No image on clipboard**: Ctrl+V passes through to Explorer normally
- **Files on clipboard** (e.g. you copied a file): normal file paste, even if the copied file is an image
- **Multiple pastes**: each one gets the next sequential number
- **Multiple instances**: only one can run at a time (enforced via mutex)
- **Non-Explorer windows**: the hook only activates for Explorer and Desktop, everything else is untouched

## Technical details

- Pure Win32 C++ with no external dependencies
- Links against system libraries only: gdiplus, ole32, shell32, shlwapi, oleaut32
- Uses `WH_KEYBOARD_LL` for the keyboard hook
- Heavy work (COM, clipboard, GDI+, file I/O) runs off the hook callback to avoid the OS timeout that silently removes slow hooks
- COM and GDI+ are initialized lazily on first paste, not at startup
- After each paste, `EmptyWorkingSet` trims physical memory back down
- Gets the current folder path from Explorer via the `IShellWindows` COM interface
- Saves PNGs using GDI+ (built into Windows)
- Prefers `CF_DIBV5` clipboard format to preserve alpha/transparency when available

## Use cases

- Save screenshots (Win+Shift+S) directly into a project folder
- Copy an image from a browser and paste it into a folder without saving it first
- Quickly dump clipboard images to the Desktop
- Batch-collect reference images by pasting them one after another

## Requirements

Windows 10 or 11 (x64 and x86).

## License

MIT
