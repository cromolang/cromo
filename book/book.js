// The Cromo Guide: theme, menu, copy buttons and search (tools/book.py).
(function () {
  var root = document.documentElement;

  // Light or dark: the choice is kept, otherwise the one of the system.
  var themeButton = document.querySelector('.theme');
  if (themeButton) {
    themeButton.addEventListener('click', function () {
      var dark = root.dataset.theme
        ? root.dataset.theme === 'dark'
        : window.matchMedia('(prefers-color-scheme: dark)').matches;
      root.dataset.theme = dark ? 'light' : 'dark';
      try { localStorage.setItem('cromo-theme', root.dataset.theme); } catch (e) {}
    });
  }

  // The chapters on narrow screens.
  var menu = document.querySelector('.menu');
  if (menu) {
    menu.addEventListener('click', function () {
      var open = document.body.classList.toggle('menu-open');
      menu.setAttribute('aria-expanded', open ? 'true' : 'false');
    });
    document.querySelectorAll('aside a').forEach(function (a) {
      a.addEventListener('click', function () { document.body.classList.remove('menu-open'); });
    });
  }

  // A button that shows the hidden lines of an example, the whole program.
  var italian = document.body.dataset.language === 'it';
  document.querySelectorAll('.code.has-hidden').forEach(function (block) {
    var button = document.createElement('button');
    button.className = 'toggle';
    button.type = 'button';
    var label = italian ? ['Mostra tutto', 'Mostra meno'] : ['Show all', 'Show less'];
    button.textContent = label[0];
    button.addEventListener('click', function () {
      var expanded = block.classList.toggle('expanded');
      button.textContent = label[expanded ? 1 : 0];
    });
    block.appendChild(button);
  });

  // A button that copies the code of a block, with its hidden lines.
  document.querySelectorAll('.code').forEach(function (block) {
    if (block.classList.contains('does-not-compile')) return;
    var button = document.createElement('button');
    button.className = 'copy';
    button.type = 'button';
    var label = italian ? ['Copia', 'Copiato'] : ['Copy', 'Copied'];
    button.textContent = label[0];
    button.addEventListener('click', function () {
      var text = block.querySelector('pre').textContent;
      navigator.clipboard.writeText(text).then(function () {
        button.textContent = label[1];
        setTimeout(function () { button.textContent = label[0]; }, 1500);
      });
    });
    block.appendChild(button);
  });

  // The active chapter stays visible in the list.
  var active = document.querySelector('.toc li.active');
  if (active && active.scrollIntoView) active.scrollIntoView({ block: 'center' });

  // Search, on the titles and the text of the chapters of this language.
  var input = document.querySelector('.search');
  var results = document.querySelector('.results');
  if (!input || !results) return;
  var index = null;
  var selected = -1;

  function load() {
    if (index) return Promise.resolve(index);
    return fetch('search.json').then(function (r) { return r.json(); }).then(function (data) { index = data; return data; });
  }

  function escapeHtml(s) {
    return s.replace(/[&<>"]/g, function (c) { return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]; });
  }

  function show(query) {
    var words = query.toLowerCase().split(/\s+/).filter(Boolean);
    if (!words.length) { results.hidden = true; return; }
    load().then(function (entries) {
      var found = [];
      entries.forEach(function (e) {
        var title = e.title.toLowerCase();
        var text = (e.text || '').toLowerCase();
        var score = 0;
        for (var i = 0; i < words.length; i++) {
          if (title.indexOf(words[i]) >= 0) score += 10;
          else if (text.indexOf(words[i]) >= 0) score += 1;
          else return;
        }
        found.push({ e: e, score: score });
      });
      found.sort(function (a, b) { return b.score - a.score; });
      found = found.slice(0, 12);
      selected = -1;
      results.innerHTML = found.length
        ? found.map(function (f) {
            return '<a href="' + f.e.url + '">' + escapeHtml(f.e.title) +
              (f.e.title !== f.e.chapter ? '<small>' + escapeHtml(f.e.chapter) + '</small>' : '') + '</a>';
          }).join('')
        : '<p>' + escapeHtml(input.dataset.empty) + '</p>';
      results.hidden = false;
    });
  }

  input.addEventListener('input', function () { show(input.value); });
  input.addEventListener('focus', function () { if (input.value) show(input.value); });
  input.addEventListener('keydown', function (event) {
    var links = results.querySelectorAll('a');
    if (event.key === 'ArrowDown' || event.key === 'ArrowUp') {
      if (!links.length) return;
      event.preventDefault();
      selected = (selected + (event.key === 'ArrowDown' ? 1 : links.length - 1)) % links.length;
      links.forEach(function (a, i) { a.classList.toggle('selected', i === selected); });
    } else if (event.key === 'Enter' && links.length) {
      location.href = links[Math.max(selected, 0)].getAttribute('href');
    } else if (event.key === 'Escape') {
      results.hidden = true;
      input.blur();
    }
  });
  document.addEventListener('click', function (event) {
    if (!results.contains(event.target) && event.target !== input) results.hidden = true;
  });
  document.addEventListener('keydown', function (event) {
    if (event.key === '/' && document.activeElement !== input) {
      event.preventDefault();
      input.focus();
    }
  });
})();
