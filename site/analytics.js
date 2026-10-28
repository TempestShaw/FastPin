(() => {
  const debug = new URLSearchParams(location.search).get('analytics_debug') === '1';
  const production = location.hostname === 'tempestshaw.github.io' && location.pathname.startsWith('/FastPin/');
  if (!production && !debug) return;

  window.dataLayer = window.dataLayer || [];
  function gtag() { window.dataLayer.push(arguments); }
  window.gtag = gtag;
  gtag('js', new Date());
  gtag('config', 'G-1H324QQV06', {
    allow_google_signals: false,
    allow_ad_personalization_signals: false,
    cookie_path: '/FastPin/',
    ...(debug ? { debug_mode: true } : {}),
  });
  const script = document.createElement('script');
  script.async = true;
  script.src = 'https://www.googletagmanager.com/gtag/js?id=G-1H324QQV06';
  document.head.appendChild(script);
})();
