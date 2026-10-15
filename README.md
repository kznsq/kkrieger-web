# .kkrieger in the browser

The 2004 first-person shooter **.kkrieger** by .theprodukkt (96 KB, Breakpoint 2004), running in a web browser straight from the original executable. The page unpacks `pno0001.exe`, reads the game data inside it and plays it with Farbrausch's published player code compiled to WebAssembly, with WebGL 2 graphics and Web Audio sound (the V2 synthesizer).

The game itself is not included: you drop your own copy of `pno0001.exe` (from the `kkrieger-beta.zip` release) onto the page.

## Running

```sh
cd web
python3 serve.py
```

Open http://127.0.0.1:8737/ and drop `pno0001.exe` onto the page. Generation takes 8 to 15 seconds; then click the picture to start. W A S D and the mouse to play, Esc for the game menu.

`web/kkrieger.wasm` is prebuilt. To rebuild it you need a Clang with the WebAssembly target; see [web/README.md](web/README.md).

## What is here

- `web/`: the browser player (WebAssembly entry points, WebGL 2 back end with a Direct3D 9 shader translator, the 2004 data loader and unpacker, V2 sound, the page). [web/README.md](web/README.md) explains how it works and every change that was needed.
- Everything else: Farbrausch's `werkkzeug3_kkrieger` engine and game sources from [fr_public](https://github.com/farbrausch/fr_public), with the changes the port needed. The tag `pristine` marks the unchanged copies.

## Licences

Farbrausch's code is BSD-licensed (`LICENSE.txt`); the V2 synthesizer is public domain (`web/v2/LICENSE.txt`); `web/kkrieger3383.kx` comes from Farbrausch's .kkrieger project data (CC-BY 3.0). The port's own code is under the same BSD licence. .kkrieger is © .theprodukkt.
