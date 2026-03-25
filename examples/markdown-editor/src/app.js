// Simple markdown parser — no dependencies
function parseMarkdown(md) {
  // Sanitize input first to prevent XSS, then apply markdown transforms
  let html = escapeHtml(md)
    // Code blocks (fenced)
    .replace(/```(\w*)\n([\s\S]*?)```/g, (_, lang, code) =>
      `<pre><code class="lang-${lang}">${code.trim()}</code></pre>`)
    // Inline code
    .replace(/`([^`]+)`/g, '<code>$1</code>')
    // Headings
    .replace(/^### (.+)$/gm, '<h3>$1</h3>')
    .replace(/^## (.+)$/gm, '<h2>$1</h2>')
    .replace(/^# (.+)$/gm, '<h1>$1</h1>')
    // Bold & italic
    .replace(/\*\*\*(.+?)\*\*\*/g, '<strong><em>$1</em></strong>')
    .replace(/\*\*(.+?)\*\*/g, '<strong>$1</strong>')
    .replace(/\*(.+?)\*/g, '<em>$1</em>')
    // Links & images
    .replace(/!\[([^\]]*)\]\(([^)]+)\)/g, '<img src="$2" alt="$1">')
    .replace(/\[([^\]]+)\]\(([^)]+)\)/g, '<a href="$2">$1</a>')
    // Blockquotes
    .replace(/^&gt; (.+)$/gm, '<blockquote><p>$1</p></blockquote>')
    // Horizontal rule
    .replace(/^---$/gm, '<hr>')
    // Unordered list items
    .replace(/^[*-] (.+)$/gm, '<li>$1</li>')
    // Paragraphs — wrap remaining lines
    .replace(/^(?!<[hpuolbdi]|<li|<hr|<pre|<block)(.+)$/gm, '<p>$1</p>')

  // Wrap consecutive <li> in <ul>
  html = html.replace(/(<li>.*<\/li>\n?)+/g, (match) => `<ul>${match}</ul>`)

  return html
}

function escapeHtml(str) {
  return str.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
}

// State
let currentPath = null
let isDirty = false

const editor = document.getElementById('editor')
const preview = document.getElementById('preview')
const filenameEl = document.getElementById('filename')
const statusEl = document.getElementById('status')

// Live preview
editor.addEventListener('input', () => {
  preview.innerHTML = parseMarkdown(editor.value)
  setDirty(true)
})

function setDirty(dirty) {
  isDirty = dirty
  statusEl.textContent = dirty ? 'Modified' : 'Saved'
}

function setFile(path, content) {
  currentPath = path
  filenameEl.textContent = path ? path.split('/').pop() : 'Untitled.md'
  editor.value = content
  preview.innerHTML = parseMarkdown(content)
  setDirty(false)
}

// Open file
async function openFile() {
  const result = await lightshell.dialog.open({
    filters: [{ name: 'Markdown', extensions: ['md', 'markdown', 'txt'] }]
  })
  if (!result) return
  const path = Array.isArray(result) ? result[0] : result
  const content = await lightshell.fs.readFile(path)
  setFile(path, content)
}

// Save file
async function saveFile() {
  if (!currentPath) {
    const path = await lightshell.dialog.save({
      defaultPath: 'untitled.md',
      filters: [{ name: 'Markdown', extensions: ['md'] }]
    })
    if (!path) return
    currentPath = path
    filenameEl.textContent = path.split('/').pop()
  }
  await lightshell.fs.writeFile(currentPath, editor.value)
  setDirty(false)
  lightshell.notify.send('Saved', `${filenameEl.textContent} saved successfully`)
}

// New file
function newFile() {
  setFile(null, '')
  editor.focus()
}

// Button handlers
document.getElementById('btn-open').addEventListener('click', openFile)
document.getElementById('btn-save').addEventListener('click', saveFile)
document.getElementById('btn-new').addEventListener('click', newFile)

// Keyboard shortcuts
document.addEventListener('keydown', (e) => {
  if ((e.metaKey || e.ctrlKey) && e.key === 'o') {
    e.preventDefault()
    openFile()
  }
  if ((e.metaKey || e.ctrlKey) && e.key === 's') {
    e.preventDefault()
    saveFile()
  }
})

// Start with sample content
setFile(null, `# Welcome to Markdown Editor

This is a **LightShell** desktop app built with plain JavaScript.

## Features

- Live markdown preview
- Open and save files with native dialogs
- Keyboard shortcuts: **Cmd+O** to open, **Cmd+S** to save
- Lightweight — the entire app is under 5MB

## Code Example

\`\`\`js
const content = await lightshell.fs.readFile('./readme.md')
lightshell.notify.send('Done', 'File loaded!')
\`\`\`

> LightShell apps use system webviews — no bundled browser.

---

Start editing to see the live preview update.
`)
