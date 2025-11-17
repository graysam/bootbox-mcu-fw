function renderControls(container, module) {
  const grid = document.createElement('div');
  grid.className = 'control-grid';
  (module.controls || []).forEach((ctrl) => {
    const card = document.createElement('div');
    card.className = 'control';
    const label = document.createElement('div');
    label.className = 'label';
    label.textContent = ctrl.label || ctrl.id;
    const meta = document.createElement('div');
    meta.className = 'meta';
    const range = typeof ctrl.min === 'number' && typeof ctrl.max === 'number'
      ? `${ctrl.min} → ${ctrl.max}${ctrl.unit ? ' ' + ctrl.unit : ''}`
      : ctrl.unit || '';
    meta.textContent = `${ctrl.type || 'slider'} • ${range}`;
    card.appendChild(label);
    card.appendChild(meta);

    const badges = document.createElement('div');
    badges.className = 'badges';
    const addr = document.createElement('span');
    addr.className = 'badge';
    addr.textContent = ctrl.address ? `0x${ctrl.address.toString(16).toUpperCase()}` : 'UI-only';
    const fmt = document.createElement('span');
    fmt.className = 'badge';
    fmt.textContent = ctrl.format || 'fixed5.23';
    badges.appendChild(addr);
    badges.appendChild(fmt);
    card.appendChild(badges);

    grid.appendChild(card);
  });
  container.appendChild(grid);
}

window.renderPreview = function renderPreview(data) {
  if (typeof data === 'string') {
    try {
      data = JSON.parse(data);
    } catch (err) {
      console.error('Preview JSON parse error', err);
      return;
    }
  }
  const root = document.getElementById('preview-root');
  root.innerHTML = '';
  const modules = data?.modules || [];
  if (!modules.length) {
    const placeholder = document.createElement('div');
    placeholder.className = 'placeholder';
    placeholder.textContent = 'Add modules to the layout to see them here.';
    root.appendChild(placeholder);
    return;
  }
  modules.forEach((module) => {
    const card = document.createElement('div');
    card.className = 'module-card';
    const title = document.createElement('h2');
    title.textContent = module.title || module.name;
    card.appendChild(title);
    if (module.description) {
      const desc = document.createElement('p');
      desc.className = 'description';
      desc.textContent = module.description;
      card.appendChild(desc);
    }
    renderControls(card, module);
    root.appendChild(card);
  });
};
