(function () {
  const KEY = 'guardianangel-theme';
  const btn = document.getElementById('themeToggle');
  const meta = document.querySelector('meta[name="theme-color"]');

  function apply(theme) {
    const light = theme === 'light';
    document.body.classList.toggle('light', light);
    if (btn) btn.textContent = light ? 'Gold & Black' : 'Gold & White';
    if (meta) meta.setAttribute('content', light ? '#faf6ec' : '#080807');
  }

  const requested = new URLSearchParams(location.search).get('theme');
  let saved = 'dark';
  try { saved = localStorage.getItem(KEY) || 'dark'; } catch (e) {}
  if (requested === 'light' || requested === 'dark') saved = requested;
  apply(saved);

  if (btn) {
    btn.addEventListener('click', function () {
      const next = document.body.classList.contains('light') ? 'dark' : 'light';
      try { localStorage.setItem(KEY, next); } catch (e) {}
      apply(next);
    });
  }
})();
