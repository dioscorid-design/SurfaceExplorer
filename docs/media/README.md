# docs/media

Images and videos published on the site
(https://dioscorid-design.github.io/SurfaceExplorer/), referenced by
`gallery.html` and `videos.html` as `media/<name>`.

**Files in here must be committed to show up on the site.** GitHub Pages serves
only what is in the repository: a file left uncommitted gives a 404, even though
the page looks right locally. The full procedure is in `docs/tools/README.md`.

## Videos: preview here, full version on YouTube

The videos page does not host full renders. Each entry is a **silent ~5 second
clip that loops**, plus a button to the complete version on YouTube. That keeps
the site light while the long high-resolution cut stays off the repository.

To build a preview:

    ./docs/tools/make-preview.sh presets/renders/3-torus.mp4 3-torus 12

Arguments: source file, output name, start second (default 5) and duration
(default 5). It writes `<name>.mp4` and `<name>-poster.jpg` in here. Then copy
the HTML block from the comment inside `videos.html` and add the YouTube link.

By hand:

    ffmpeg -ss 12 -t 5 -i input.mp4 -vf "scale=1920:-2,fps=24" \
           -c:v libx264 -crf 26 -preset slow -movflags +faststart \
           -an docs/media/name.mp4

### Measured sizes

From a real 30 s 1080p60 export (22 MB), taking 5 seconds out of it:

| format | size |
|---|---|
| 1080p24 crf 26 | **0.95 MB** (recommended) |
| 1080p24 crf 30 | 0.63 MB |
| 720p24 crf 26 | 0.41 MB |
| 1080p30 crf 26 | 1.02 MB |

**Duration** drives the size, not resolution: dropping to 720p saves half a
megabyte and throws away half the geometric detail, which on these surfaces is
exactly what is worth showing. Stay at 1080p and shorten the clip instead.

At these sizes ten previews fit in about 10 MB.

### Details that matter

- `-movflags +faststart` moves the index to the front of the file: playback
  starts without downloading all of it.
- `-an` strips the audio. Previews autoplay, and browsers block autoplay with
  sound.
- The markup needs `loop muted autoplay playsinline`: without `muted` it will
  not start, without `playsinline` iOS opens the fullscreen player.

## Images

PNG for flat-shaded surfaces and wireframes, JPEG (quality ~85) for gradients
and textures. 1600 px wide is already generous: the grid never shows them
larger. Keep each under ~1 MB.

## Limits

Site 1 GB, single file 100 MB, bandwidth 100 GB/month. Committed files stay in
git history even after deletion, so compress **before** adding them.
