// desktop.js — Desktop icons, taskbar, window content loading
(() => {
  // Clock
  function updateClock() {
    const el = document.querySelector('.taskbar-clock')
    if (!el) return
    const now = new Date()
    const h = now.getHours()
    const m = String(now.getMinutes()).padStart(2, '0')
    const ampm = h >= 12 ? 'PM' : 'AM'
    const h12 = h % 12 || 12
    el.textContent = `${h12}:${m} ${ampm}`
  }
  updateClock()
  setInterval(updateClock, 10000)

  // Desktop icon definitions — left side (single column)
  const icons = [
    { id: 'getting-started', label: 'getting-started.md', icon: '\uD83D\uDCD6', action: openGettingStarted },
    { id: 'examples',        label: 'examples/',          icon: '\uD83D\uDDC2\uFE0F', action: openExamples },
    { id: 'playground',      label: 'playground.app',     icon: '\u26A1',       action: openPlayground },
    { id: 'docs',            label: 'docs/',              icon: '\uD83D\uDCDA', action: () => window.open('/docs/', '_blank') },
    { id: 'benchmarks',      label: 'benchmarks.csv',     icon: '\uD83D\uDCC8', action: openBenchmarks },
    { id: 'about',           label: 'about.txt',          icon: '\uD83D\uDCA1', action: openAbout },
    { id: 'github',          label: 'github.link',        icon: '<img src="assets/icons/github.svg" width="36" height="36" alt="GitHub" style="filter: brightness(0.2)">', action: () => window.open('https://github.com/lightshell-dev/lightshell', '_blank') },
  ]

  // Right-side icons
  const rightIcons = [
    { id: 'showcase', label: 'showcase/', icon: '\uD83C\uDFC6', action: openShowcase },
    { id: 'prompts',  label: 'prompts.txt', icon: '\uD83E\uDD16', action: openPrompts },
  ]

  // Helper to render icon elements
  function renderIcon(def, container) {
    const el = document.createElement('div')
    el.className = 'desktop-icon'
    el.setAttribute('role', 'button')
    el.setAttribute('tabindex', '0')
    el.setAttribute('aria-label', def.label)
    el.innerHTML = `<div class="icon-img">${def.icon}</div><div class="icon-label">${def.label}</div>`
    el.addEventListener('dblclick', def.action)
    el.addEventListener('keydown', (e) => { if (e.key === 'Enter') def.action() })
    let clickTimer = null
    el.addEventListener('click', () => {
      if (window.innerWidth <= 768) { def.action(); return }
      if (clickTimer) { clearTimeout(clickTimer); clickTimer = null; return }
      clickTimer = setTimeout(() => { clickTimer = null }, 400)
    })
    container.appendChild(el)
  }

  // Render left icons (horizontal row)
  const iconGrid = document.querySelector('.desktop-icons')
  if (iconGrid) icons.forEach(def => renderIcon(def, iconGrid))

  // Render right icons
  const rightGrid = document.querySelector('.desktop-icons-right')
  if (rightGrid) rightIcons.forEach(def => renderIcon(def, rightGrid))

  // Open welcome window on load
  setTimeout(() => {
    WindowManager.open({
      id: 'welcome',
      title: 'Welcome to LightShell',
      icon: '\u26A1',
      width: 520,
      height: 340,
      content: document.getElementById('content-welcome')?.innerHTML || '',
      resizable: true,
    })
  }, 300)

  // Window content openers
  function openGettingStarted() {
    WindowManager.open({
      id: 'getting-started',
      title: 'getting-started.md',
      icon: '\uD83D\uDCC4',
      width: 660,
      height: 520,
      x: 140,
      y: 60,
      content: document.getElementById('content-getting-started')?.innerHTML || '',
    })
  }

  function openExamples() {
    WindowManager.open({
      id: 'examples',
      title: 'examples/',
      icon: '\uD83D\uDCC1',
      width: 560,
      height: 440,
      x: 200,
      y: 100,
      content: document.getElementById('content-examples')?.innerHTML || '',
    })
  }

  function openPlayground() {
    window.open('playground.html', '_blank')
  }

  function openPrompts() {
    WindowManager.open({
      id: 'prompts',
      title: 'prompts.txt',
      icon: '\uD83E\uDD16',
      width: 600,
      height: 540,
      x: 240,
      y: 50,
      content: document.getElementById('content-prompts')?.innerHTML || '',
    })
  }

  function openShowcase() {
    WindowManager.open({
      id: 'showcase',
      title: 'showcase/',
      icon: '\uD83C\uDFC6',
      width: 620,
      height: 520,
      x: 160,
      y: 70,
      content: document.getElementById('content-showcase')?.innerHTML || '',
    })
  }

  function openBenchmarks() {
    WindowManager.open({
      id: 'benchmarks',
      title: 'benchmarks.csv',
      icon: '\uD83D\uDCCA',
      width: 640,
      height: 380,
      x: 180,
      y: 120,
      content: document.getElementById('content-benchmarks')?.innerHTML || '',
    })
  }

  function openAbout() {
    WindowManager.open({
      id: 'about',
      title: 'about.txt',
      icon: '\uD83D\uDCC4',
      width: 560,
      height: 480,
      x: 220,
      y: 80,
      content: document.getElementById('content-about')?.innerHTML || '',
    })
  }

  // Demo app openers
  const demoApps = {
    'markdown-editor': { title: 'Markdown Editor', icon: '\uD83D\uDCDD', width: 700, height: 460 },
    'todo-app':        { title: 'Todo App',        icon: '\uD83D\uDCCB', width: 420, height: 440 },
    'color-picker':    { title: 'Color Picker',    icon: '\uD83C\uDFA8', width: 360, height: 420 },
    'pomodoro':        { title: 'Pomodoro Timer',  icon: '\u23F1',       width: 340, height: 320 },
    'system-monitor':  { title: 'System Monitor',  icon: '\uD83D\uDCBB', width: 440, height: 400 },
    'json-viewer':     { title: 'JSON Viewer',     icon: '\uD83D\uDCF0', width: 460, height: 420 },
  }

  window.openDemoApp = function(id) {
    const app = demoApps[id]
    if (!app) return
    const tpl = document.getElementById('demo-' + id)
    if (!tpl) return

    WindowManager.open({
      id: 'demo-' + id,
      title: app.title,
      icon: app.icon,
      width: app.width,
      height: app.height,
      x: 200 + Math.random() * 100,
      y: 60 + Math.random() * 80,
      content: tpl.innerHTML,
    })

    // Post-open initialization
    setTimeout(() => initDemoApp(id), 50)
  }

  function initDemoApp(id) {
    if (id === 'markdown-editor') {
      const input = document.getElementById('md-input')
      const preview = document.getElementById('md-preview')
      if (!input || !preview) return
      function renderMd(text) {
        // Sanitize input to prevent XSS before applying markdown transforms
        const escaped = text
          .replace(/&/g, '&amp;')
          .replace(/</g, '&lt;')
          .replace(/>/g, '&gt;')
          .replace(/"/g, '&quot;')
        return escaped
          .replace(/^### (.+)$/gm, '<h3>$1</h3>')
          .replace(/^## (.+)$/gm, '<h2>$1</h2>')
          .replace(/^# (.+)$/gm, '<h1>$1</h1>')
          .replace(/\*\*(.+?)\*\*/g, '<strong>$1</strong>')
          .replace(/\*(.+?)\*/g, '<em>$1</em>')
          .replace(/`(.+?)`/g, '<code style="background:rgba(0,0,0,0.06);padding:1px 5px;border-radius:3px;font-size:0.85em">$1</code>')
          .replace(/^&gt; (.+)$/gm, '<blockquote style="border-left:3px solid rgba(0,0,0,0.15);padding-left:12px;color:var(--text-secondary)">$1</blockquote>')
          .replace(/^- (.+)$/gm, '<li>$1</li>')
          .replace(/(<li>.*<\/li>)/s, '<ul style="padding-left:1.2rem">$1</ul>')
          .replace(/\n\n/g, '<br><br>')
          .replace(/\n/g, '<br>')
      }
      function update() { preview.innerHTML = renderMd(input.value) }
      input.addEventListener('input', update)
      update()
    }

    if (id === 'pomodoro') {
      let seconds = 25 * 60, running = false, interval = null
      const timeEl = document.getElementById('pomo-time')
      const btnEl = document.getElementById('pomo-start')
      const labelEl = document.getElementById('pomo-label')
      if (!timeEl) return
      function fmt(s) { return String(Math.floor(s/60)).padStart(2,'0') + ':' + String(s%60).padStart(2,'0') }
      window.togglePomo = function() {
        running = !running
        btnEl.textContent = running ? 'Pause' : 'Start'
        if (running) {
          interval = setInterval(() => {
            seconds--
            timeEl.textContent = fmt(seconds)
            if (seconds <= 0) { clearInterval(interval); running = false; btnEl.textContent = 'Start'; alert('Session complete!') }
          }, 1000)
        } else { clearInterval(interval) }
      }
      window.resetPomo = function() {
        clearInterval(interval); running = false; seconds = 25 * 60
        timeEl.textContent = fmt(seconds); btnEl.textContent = 'Start'
      }
    }

    if (id === 'color-picker') {
      window.updateColor = function() {
        const r = document.getElementById('color-r').value
        const g = document.getElementById('color-g').value
        const b = document.getElementById('color-b').value
        document.getElementById('color-r-val').textContent = r
        document.getElementById('color-g-val').textContent = g
        document.getElementById('color-b-val').textContent = b
        const hex = '#' + [r,g,b].map(v => Number(v).toString(16).padStart(2,'0')).join('').toUpperCase()
        document.getElementById('color-hex').textContent = hex
        document.getElementById('color-swatch').style.background = hex
        document.getElementById('color-swatch').style.boxShadow = `0 8px 24px ${hex}44`
      }
      window.copyColor = function() {
        const hex = document.getElementById('color-hex').textContent
        navigator.clipboard.writeText(hex).then(() => {
          const el = document.getElementById('color-hex')
          const orig = el.textContent; el.textContent = 'Copied!'; setTimeout(() => el.textContent = orig, 1000)
        })
      }
    }

    if (id === 'todo-app') {
      let todos = []
      window.addTodo = function() {
        const input = document.getElementById('todo-input')
        if (!input.value.trim()) return
        todos.push({ text: input.value.trim(), done: false })
        input.value = ''
        renderTodos()
      }
      window.toggleTodo = function(i) { todos[i].done = !todos[i].done; renderTodos() }
      window.deleteTodo = function(i) { todos.splice(i, 1); renderTodos() }
      function renderTodos() {
        const list = document.getElementById('todo-list')
        const empty = document.getElementById('todo-empty')
        if (!list) return
        empty.style.display = todos.length ? 'none' : 'block'
        list.innerHTML = todos.map((t, i) => `
          <div style="display:flex;align-items:center;gap:10px;padding:10px 14px;border-radius:10px;margin-bottom:6px;background:rgba(0,0,0,0.02);transition:all 0.15s">
            <input type="checkbox" ${t.done ? 'checked' : ''} onchange="toggleTodo(${i})" style="width:18px;height:18px;accent-color:var(--accent);cursor:pointer">
            <span style="flex:1;font-size:0.85rem;${t.done ? 'text-decoration:line-through;color:var(--text-muted)' : ''}">${t.text}</span>
            <button onclick="deleteTodo(${i})" style="background:none;border:none;font-size:1rem;cursor:pointer;color:var(--text-muted);padding:2px 6px">&times;</button>
          </div>
        `).join('')
      }
    }

    if (id === 'system-monitor') {
      let sec = 0
      const uptimeEl = document.getElementById('sysmon-uptime')
      if (uptimeEl) setInterval(() => { sec++; uptimeEl.textContent = sec + 's' }, 1000)
    }
  }

  // Copy prompt text to clipboard
  window.copyPrompt = function(btn) {
    const text = btn.parentElement.querySelector('.prompt-text p').textContent
    navigator.clipboard.writeText(text).then(() => {
      const orig = btn.textContent
      btn.textContent = 'Copied!'
      setTimeout(() => { btn.textContent = orig }, 1500)
    })
  }

  // Expose for playground link in welcome window
  window.openPlayground = openPlayground
  window.openGettingStarted = openGettingStarted
})()
