// Small DOM helpers. No framework, but not raw innerHTML either: every element
// is built with createElement and every user-supplied string is set with
// textContent, so a student named `<script>` renders as that name.
//
// The one rule that matters for security: never interpolate server or user data
// into an HTML string. Use el()/text() below.

/**
 * el('button', { class: 'primary', onclick: fn }, 'Save')
 *
 * - `class`, `id`, `type`, `disabled`, `value`, `placeholder`, `name`, `href`,
 *   `title`, `autocomplete`, `rows`, `colSpan` are set as properties.
 * - `data-*` and `aria-*` are set as attributes.
 * - `on*` keys attach listeners.
 * - Children may be strings (text nodes), nodes, or arrays of either; null and
 *   undefined are skipped so conditional children can be written inline.
 */
export function el(tag, props = {}, ...children) {
  const node = document.createElement(tag);
  for (const [key, value] of Object.entries(props || {})) {
    if (value === null || value === undefined || value === false) continue;
    if (key.startsWith('on') && typeof value === 'function') {
      node.addEventListener(key.slice(2).toLowerCase(), value);
    } else if (key === 'class' || key === 'className') {
      node.className = value;
    } else if (key === 'text') {
      node.textContent = value;
    } else if (key === 'dataset') {
      Object.assign(node.dataset, value);
    } else if (key.startsWith('data-') || key.startsWith('aria-') || key === 'role') {
      node.setAttribute(key, value === true ? 'true' : value);
    } else if (key in node) {
      node[key] = value;
    } else {
      node.setAttribute(key, value);
    }
  }
  append(node, children);
  return node;
}

function append(parent, children) {
  for (const child of children.flat(Infinity)) {
    if (child === null || child === undefined || child === false) continue;
    parent.append(child instanceof Node ? child : document.createTextNode(String(child)));
  }
  return parent;
}

export function clear(node) {
  node.replaceChildren();
  return node;
}

export function text(value) {
  return document.createTextNode(value == null ? '' : String(value));
}

/** Replaces a container's children in one step. */
export function mount(container, ...children) {
  clear(container);
  append(container, children);
  return container;
}

/** A labelled form field with an inline error slot (SRS-NF-003). */
export function field(labelText, control, { hint = '', error = '', name = '' } = {}) {
  const id = control.id || `f-${Math.random().toString(36).slice(2, 9)}`;
  control.id = id;
  // A stable name makes the field addressable by tests, autofill and form
  // serialisation. Derived from the label unless the caller supplied one.
  if (!control.name) {
    control.name = name || labelText.toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '');
  }
  const errorNode = el('p', { class: 'field-error', role: 'alert', hidden: !error }, error);
  const hintNode = hint ? el('p', { class: 'field-hint' }, hint) : null;
  const wrapper = el(
    'div',
    { class: `field${error ? ' has-error' : ''}` },
    el('label', { for: id }, labelText),
    control,
    hintNode,
    errorNode,
  );
  return wrapper;
}

/** Shows or clears the inline error for a field wrapper produced by field(). */
export function setFieldError(wrapper, message) {
  const node = wrapper.querySelector('.field-error');
  const control = wrapper.querySelector('input, select, textarea');
  if (!node) return;
  node.textContent = message || '';
  node.hidden = !message;
  wrapper.classList.toggle('has-error', Boolean(message));
  if (control) {
    if (message) control.setAttribute('aria-invalid', 'true');
    else control.removeAttribute('aria-invalid');
  }
}

/** A non-blocking status line. `tone` is 'info' | 'error' | 'success'. */
export function statusBar() {
  const node = el('p', { class: 'status', role: 'status' });
  return {
    node,
    set(message, tone = 'info') {
      node.textContent = message || '';
      node.className = `status ${tone}`;
      node.hidden = !message;
    },
    clear() {
      this.set('', 'info');
    },
  };
}

/**
 * A modal dialog that behaves like the Qt one it replaces: modal, cancellable
 * with Escape or the Cancel button, and focus trapped by <dialog> itself.
 */
export function openDialog(title, body, { actions = [], onClose = null } = {}) {
  const closeButton = el('button', { type: 'button', class: 'ghost', 'aria-label': 'Close' }, '×');
  // A caller that passes its own footer (a form does) must not also get an
  // empty one below it, which is what a stray <footer> used to render.
  const footer = actions.length ? el('footer', { class: 'dialog-actions' }, ...actions) : null;

  const dialog = el(
    'dialog',
    { class: 'modal' },
    el('header', { class: 'dialog-head' }, el('h2', {}, title), closeButton),
    el('div', { class: 'dialog-body' }, body),
    footer,
  );

  let closed = false;
  const close = (value) => {
    if (closed) return;
    closed = true;
    dialog.close();
    dialog.remove();
    if (onClose) onClose(value);
  };

  closeButton.addEventListener('click', () => close(null));
  dialog.addEventListener('cancel', (event) => {
    event.preventDefault();
    close(null);
  });
  dialog.addEventListener('close', () => {
    if (!closed) {
      closed = true;
      dialog.remove();
      if (onClose) onClose(null);
    }
  });

  document.body.append(dialog);
  dialog.showModal();
  const first = dialog.querySelector('input, select, textarea, button.primary');
  if (first) first.focus();

  return { dialog, close, body: dialog.querySelector('.dialog-body'), footer };
}

/** Confirmation for a destructive action. Resolves true when confirmed. */
export function confirmDialog({ title, message, confirmLabel = 'Confirm', danger = false }) {
  return new Promise((resolve) => {
    const cancel = el('button', { type: 'button', class: 'ghost' }, 'Cancel');
    const confirm = el(
      'button',
      { type: 'button', class: danger ? 'danger' : 'primary' },
      confirmLabel,
    );
    const handle = openDialog(title, el('p', {}, message), {
      actions: [cancel, confirm],
      onClose: (value) => resolve(value === true),
    });
    cancel.addEventListener('click', () => handle.close(false));
    confirm.addEventListener('click', () => handle.close(true));
    confirm.focus();
  });
}