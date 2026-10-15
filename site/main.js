// Progressive enhancement only: the page works without this script.
(() => {
  const repo = "TempestShaw/FastPin";
  const platform = (navigator.userAgentData && navigator.userAgentData.platform) || navigator.platform || navigator.userAgent;
  const isMac = /mac|iphone|ipad/i.test(platform);
  const isWindows = /win/i.test(platform);

  // FastPin uses physical Control on both systems; show the native symbols on a Mac.
  const keyLabels = isMac ? { ctrl: "⌃", shift: "⇧" } : { ctrl: "Ctrl", shift: "Shift" };
  document.querySelectorAll("kbd[data-key]").forEach((kbd) => {
    if (kbd.dataset.key === "control-cap") {
      // Large keycap: symbol in the corner, name below, like a Mac keyboard.
      kbd.replaceChildren(Object.assign(document.createElement("b"), { textContent: isMac ? "⌃" : "" }), isMac ? "control" : "Ctrl");
      return;
    }
    const label = keyLabels[kbd.dataset.key];
    if (label) kbd.textContent = label;
  });

  const recommended = document.querySelector(`.download[data-os="${isMac ? "mac" : isWindows ? "windows" : ""}"]`);
  const primary = document.querySelector("[data-primary-download]");
  if (recommended) {
    recommended.classList.add("recommended");
    if (primary) primary.textContent = isMac ? "Download for Mac" : "Download for Windows";
  }

  // Seven-by-seven pixel loupe for the color tile.
  const loupe = document.querySelector("[data-loupe]");
  if (loupe) {
    const palette = ["#E9EDEB", "#D5DDD8", "#B6C1BB", "#19D98A", "#12B874", "#FFFFFF"];
    for (let i = 0; i < 49; i += 1) {
      const pixel = document.createElement("i");
      const distance = Math.abs((i % 7) - 3) + Math.abs(Math.floor(i / 7) - 3);
      pixel.style.background = i === 24 ? "#19D98A" : palette[(i * 7 + distance * 3) % palette.length];
      if (i === 24) pixel.className = "center";
      loupe.appendChild(pixel);
    }
  }

  const nav = document.querySelector(".nav");
  const onScroll = () => nav && nav.classList.toggle("scrolled", window.scrollY > 8);
  window.addEventListener("scroll", onScroll, { passive: true });
  onScroll();

  // Point the download buttons at the newest release, previews included.
  fetch(`https://api.github.com/repos/${repo}/releases?per_page=10`, { headers: { Accept: "application/vnd.github+json" } })
    .then((response) => (response.ok ? response.json() : Promise.reject(new Error(`GitHub ${response.status}`))))
    .then((releases) => {
      const release = releases.find((item) => !item.draft);
      if (!release) return;
      document.querySelectorAll("[data-asset]").forEach((link) => {
        const asset = release.assets.find((item) => item.name === link.dataset.asset);
        if (asset && asset.browser_download_url.startsWith(`https://github.com/${repo}/releases/download/`))
          link.href = asset.browser_download_url;
      });
      const version = release.tag_name.replace(/^v/, "");
      const line = document.querySelector("[data-release-line]");
      if (line) line.textContent = `Version ${version} · No account, no tracking`;
      const note = document.querySelector("[data-release-note]");
      if (note) {
        // Build nodes rather than HTML so API text can never inject markup.
        const notes = document.createElement("a");
        notes.href = `https://github.com/${repo}/releases/tag/${encodeURIComponent(release.tag_name)}`;
        notes.textContent = "Release notes ↗";
        note.replaceChildren(`Version ${version}${release.prerelease ? " (preview)" : ""}. `, notes);
      }
    })
    .catch((error) => console.warn("FastPin: using the releases page for downloads.", error));
})();
