# docs/tools

Tooling for the site at https://dioscorid-design.github.io/SurfaceExplorer/

---

## How publishing works (read this first)

**The site IS the repository.** GitHub Pages serves only files that are
**committed and pushed**: there is no upload panel. A file sitting on your disk
does not exist as far as the site is concerned — the page asks for it and gets a
404.

So every publication takes these four steps:

    1. put the file in docs/media/
    2. add the HTML block to the page (docs/gallery.html or docs/videos.html)
    3. git add the FILE **and** the PAGE          <-- the step people forget
    4. git commit && git push

Skipping step 3 is the classic mistake: the published page shows a broken image
or a video that will not play, while everything looks fine locally.

The site rebuilds itself after a push (about a minute). To check:

    curl -o /dev/null -w "%{http_code}\n" https://dioscorid-design.github.io/SurfaceExplorer/media/NAME.mp4

### Adding an image

    1. save it into docs/media/ (PNG for flat shading and wireframes, JPEG q~85
       for gradients and textures; 1600 px wide is plenty, keep under ~1 MB)
    2. in docs/gallery.html copy the <figure> block from the template (it is in
       an HTML comment at the top), then change file name and caption
    3. git add docs/media/NAME.png docs/gallery.html
    4. git commit && git push

### Adding a video

    1. ./docs/tools/make-preview.sh <video> <name> <start-second>
       -> writes docs/media/<name>.mp4 and <name>-poster.jpg
    2. upload the FULL video to YouTube and copy its link
    3. in docs/videos.html uncomment the template block, fill in name and link
    4. git add docs/media/<name>.mp4 docs/media/<name>-poster.jpg docs/videos.html
    5. git commit && git push

### What to commit and what not to

The criterion is **weight**, not file type: whatever enters git history stays
there — it survives deletion, and everyone who clones downloads it.

| file | in the repo? |
|---|---|
| screenshot PNG/JPEG (~0.5 MB) | yes, that is the point |
| 5 s preview at 1080p24 (~1 MB) | yes |
| original 1080p60 export (22 MB) | **never** — keep it in `presets/renders/`, untracked |
| full-length video | no — upload to YouTube and link it from the page |

---

## make-preview.sh

Builds the preview clip for `docs/videos.html`:

    ./docs/tools/make-preview.sh presets/renders/3-torus.mp4 3-torus 12

Arguments: source file, output name, start second (default 5), duration
(default 5). Writes `docs/media/<name>.mp4` and `docs/media/<name>-poster.jpg`.

It trims the clip, scales it to 1080p24 at crf 26, strips the audio and grabs
the poster frame. It touches neither the pages nor git: the HTML block and the
`git add` are still yours to do (see above).

Sizes measured on a real 30 s 1080p60 export (22 MB), taking 5 seconds out of
it: 1080p24 crf 26 = 0.95 MB, crf 30 = 0.63 MB, 720p24 = 0.41 MB. **Duration**
drives the size, not resolution: stay at 1080p and shorten the clip instead.

---

## Editing the manual pages

They are edited **by hand**, one at a time: there is no generator any more.

They are served both by the site and, as qrc resources, by the manual inside the
application, where the viewer is a `QTextBrowser`. That engine ignores most CSS
(no flexbox, no `border-left`): the visual hierarchy relies on bands built from
`bgcolor` tables, `<hr>` and inline `font-size`. See the comment at
`mainwindow.cpp:1129`.

Adding or removing a chapter also means updating `CMakeLists.txt`
(`_doc_chapters`) and `update_resources.py` (`DOC_CHAPTERS`): both carry a guard
that fails the build when a page is missing from the `.qrc`.

---

## Layout

    docs/            the published SITE
      *.html         the pages
      media/         images and video previews served to the browser
      tools/         these scripts

GitHub Pages is configured on branch `v1`, folder `/docs`, and in "deploy from a
branch" mode it accepts **only** `/` or `/docs` — no other path is allowed (the
API answers *"Must be one of the following: /, /docs"*). That is why `docs/`
cannot be renamed and `media/` cannot be moved out: the site would stop being
published.

One consequence of living inside `docs/`: **this folder is published too**.
`https://.../SurfaceExplorer/tools/make-preview.sh` is reachable. That is not a
problem — it is a script already public in the repository, with no credentials —
but it is worth knowing: no keys or private files in here.
