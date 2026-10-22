# .kkrieger in the browser

The 2004 first-person shooter **.kkrieger** by .theprodukkt (96 KB, Breakpoint 2004), running in a web browser straight from the original executable. The page unpacks `pno0001.exe`, reads the game data inside it and plays it with Farbrausch's published player code compiled to WebAssembly, with WebGL 2 graphics and Web Audio sound. The game logic and the V2 synthesizer follow the 2004 executable rather than the later published source.

The game is included: the beta release (`pno0001.exe` with its `readme.txt` and `scene.org.txt`) is in `web/original/`, unmodified, as .theprodukkt published it. The page loads it from there by itself; there is nothing to drop or choose.

## Running

```sh
cd web
python3 serve.py
```

Open http://127.0.0.1:8737/. Generation takes 8 to 15 seconds; then click the picture to start. W A S D and the mouse to play, Esc for the game menu. If you get caught in the collision, press M and then a number from 1 to 9 to respawn at a respawn point, as the original readme suggests.

`web/kkrieger.wasm` is prebuilt. To rebuild it you need a Clang with the WebAssembly target; see [web/README.md](web/README.md).

## What is here

- `web/`: the browser player (WebAssembly entry points, WebGL 2 back end with a Direct3D 9 shader translator, the 2004 data loader and unpacker, V2 sound, the page) and `web/original/`, the game itself. [web/README.md](web/README.md) explains how it works and every change that was needed.
- Everything else: Farbrausch's `werkkzeug3_kkrieger` engine and game sources from [fr_public](https://github.com/farbrausch/fr_public), with the changes the port needed. The tag `pristine` marks the unchanged copies.

## Licences

Farbrausch's code is BSD-licensed (`LICENSE.txt`); the V2 synthesizer, including its Ronan speech synthesizer, is public domain (`web/v2/LICENSE.txt`); `web/kkrieger3383.kx` comes from Farbrausch's .kkrieger project data (CC-BY 3.0). The port's own code is under the same BSD licence. .kkrieger is © .theprodukkt and freeware; its release files in `web/original/` are included unmodified, under the permission in its `readme.txt` to spread the release in its original, unmodified form free of charge, with credit to .theprodukkt.
