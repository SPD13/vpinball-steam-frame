// Backup of what is kept on the device (tables, ROMs, settings, scores...) as a zip downloaded from the device, and its restore: the zip is read
// here, and only the entries the user selects are uploaded. See steamframe/Backup.h and WebServerBackup.cpp in the application.

// In the order the page shows them. 'id' is the first folder of the entries of the zip.
const ITEMS = [
  { id: 'games', label: 'Tables', description: 'The tables and the files along them: backglasses, music, PinUP Player packs...' },
  { id: 'thumbnails', label: 'Table images', description: 'The images shown by the table picker' },
  { id: 'roms', label: 'ROMs', description: 'PinMAME ROMs, with the colorizations and the sound packs of the games' },
  { id: 'saves', label: 'Machine saves', description: 'High scores and settings saved by the games (PinMAME nvram, VPReg)' },
  { id: 'library', label: 'Library', description: 'Favorites, table names and play counts' },
  { id: 'settings', label: 'Settings', description: 'Settings of the application and of the tables, room of the lobby (not the ones of the device: displays, audio, controllers, folders)' },
  { id: 'scores', label: 'Scores and players', description: 'Leaderboards and player profiles' },
  { id: 'aliases', label: 'ROM aliases', description: 'The PinMAME alias file' }
];

const MANIFEST_NAME = 'backup.json';
const MANIFEST_FORMAT = 'vpinball-steam-frame-backup';
const UPLOAD_CHUNK_SIZE = 1024 * 1024; // The server takes up to 3 MB per request, and receives smaller ones faster
const UPLOAD_PARALLEL = 3;
const CHECK_BATCH = 500; // Entries per request asking which files are already on the device
const POLL_INTERVAL_MS = 1000;

const State = {
  playing: false,
  backupSelector: null,
  backupId: null,
  backupPoll: null,
  zip: null, // Restore: { file, entries, manifest }
  restoreSelector: null,
  restoreSession: null,
  restoreAbort: false
};

const $ = (id) => document.getElementById(id);

///////////////////////////////////////////////////////////////////////////////
// Server

// Servers that require pairing answer 401 until the code displayed by the application has been entered
async function ensurePaired() {
  for (;;) {
    const response = await fetch('/info');
    if (response.status !== 401)
      return response.ok ? response.json() : {};
    const code = window.prompt('Enter the pairing code displayed by Visual Pinball');
    if (code === null) {
      document.body.textContent = 'Pairing is required to manage this device. Reload the page to try again.';
      return null;
    }
    await fetch(`/pair?code=${encodeURIComponent(code.trim())}`, { method: 'POST' });
  }
}

// The JSON reply, or an Error with the message of the server
async function request(url, options = {}) {
  const response = await fetch(url, { cache: 'no-store', ...options });
  const text = await response.text();
  let data = null;
  try {
    data = text ? JSON.parse(text) : null;
  } catch (e) {
    data = null;
  }
  if (!response.ok) {
    const error = new Error(data?.error || text || `Error ${response.status}`);
    error.status = response.status;
    throw error;
  }
  return data;
}

function postJson(url, body) {
  return request(url, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) });
}

///////////////////////////////////////////////////////////////////////////////
// Helpers

function formatBytes(bytes) {
  if (bytes < 1024)
    return `${bytes} B`;
  const units = ['KB', 'MB', 'GB', 'TB'];
  let value = bytes / 1024, unit = 0;
  while (value >= 1024 && unit < units.length - 1) {
    value /= 1024;
    unit++;
  }
  return `${value.toFixed(value < 10 ? 1 : 0)} ${units[unit]}`;
}

function formatDate(secondsSinceEpoch) {
  return new Date(secondsSinceEpoch * 1000).toLocaleString(undefined, { day: 'numeric', month: 'short', year: 'numeric', hour: '2-digit', minute: '2-digit' });
}

function plural(count, word) {
  return `${count.toLocaleString()} ${word}${count === 1 ? '' : 's'}`;
}

function showMessage(id, message, type) {
  const element = $(id);
  element.hidden = !message;
  element.textContent = message || '';
  element.className = `backup-message${type ? ` ${type}` : ''}`;
}

function setProgress(prefix, text, fraction) {
  $(`${prefix}-progress`).hidden = false;
  $(`${prefix}-progress-text`).textContent = text;
  $(`${prefix}-progress-fill`).style.width = `${Math.max(0, Math.min(1, fraction)) * 100}%`;
}

function hideProgress(prefix) {
  $(`${prefix}-progress`).hidden = true;
}

///////////////////////////////////////////////////////////////////////////////
// Selector: the items, and the tables, with their size (shared by the backup and the restore)

// 'items': [{ id, files, bytes }], 'tables': [{ uuid, name, bytes: { item: bytes } }], 'other': { item: bytes } for the files linked to no table
function createSelector(container, items, tables, other, onChange) {
  container.innerHTML = '';
  const selector = { items: new Map(), tables: new Map(), allTables: true, otherFiles: true };

  const list = document.createElement('ul');
  list.className = 'backup-items';
  for (const definition of ITEMS) {
    const item = items.find(i => i.id === definition.id);
    if (!item)
      continue;
    const row = document.createElement('li');
    const label = document.createElement('label');
    label.className = 'backup-item';
    const checkbox = document.createElement('input');
    checkbox.type = 'checkbox';
    checkbox.checked = item.files > 0;
    checkbox.disabled = item.files === 0;
    checkbox.addEventListener('change', () => update());
    const text = document.createElement('span');
    text.className = 'backup-item-text';
    text.innerHTML = '<strong></strong><small></small>';
    text.querySelector('strong').textContent = definition.label;
    text.querySelector('small').textContent = definition.description;
    const size = document.createElement('span');
    size.className = 'backup-item-size';
    size.textContent = item.files === 0 ? 'Nothing' : `${formatBytes(item.bytes)} · ${plural(item.files, 'file')}`;
    label.append(checkbox, text, size);
    row.appendChild(label);
    list.appendChild(row);
    selector.items.set(definition.id, checkbox);
  }
  container.appendChild(list);

  // Tables: all of them, or the ones chosen in the list
  const tablesBlock = document.createElement('div');
  tablesBlock.className = 'backup-tables';
  const modes = document.createElement('div');
  modes.className = 'backup-table-modes';
  const name = `mode-${container.id}`;
  modes.innerHTML = `<label><input type="radio" name="${name}" value="all" checked> All tables</label><label><input type="radio" name="${name}" value="some"> Choose tables</label>`;
  const chooser = document.createElement('div');
  chooser.className = 'backup-table-chooser';
  chooser.hidden = true;
  const toolbar = document.createElement('div');
  toolbar.className = 'backup-table-toolbar';
  toolbar.innerHTML = '<input type="search" placeholder="Search tables" aria-label="Search tables"><button type="button" class="btn btn-secondary">All</button><button type="button" class="btn btn-secondary">None</button>';
  const [search, selectAll, selectNone] = toolbar.children;
  const tableList = document.createElement('ul');
  tableList.className = 'backup-table-list';
  const sorted = [...tables].sort((a, b) => a.name.localeCompare(b.name, undefined, { sensitivity: 'base' }));
  const sizes = new Map();
  for (const table of sorted) {
    const row = document.createElement('li');
    row.innerHTML = '<label><input type="checkbox"><span class="backup-table-name"></span><span class="backup-item-size"></span></label>';
    const checkbox = row.querySelector('input');
    checkbox.checked = true;
    checkbox.addEventListener('change', () => update());
    row.querySelector('.backup-table-name').textContent = table.name;
    row.dataset.name = table.name.toLowerCase();
    tableList.appendChild(row);
    selector.tables.set(table.uuid, checkbox);
    sizes.set(table.uuid, row.querySelector('.backup-item-size'));
  }
  const otherRow = document.createElement('label');
  otherRow.className = 'backup-other';
  otherRow.innerHTML = '<input type="checkbox" checked><span class="backup-table-name">Files that belong to no table</span><span class="backup-item-size"></span>';
  const otherCheckbox = otherRow.querySelector('input');
  otherCheckbox.addEventListener('change', () => update());
  chooser.append(toolbar, tableList, otherRow);
  tablesBlock.append(modes, chooser);
  if (tables.length > 0)
    container.appendChild(tablesBlock);

  for (const radio of modes.querySelectorAll('input'))
    radio.addEventListener('change', () => {
      selector.allTables = modes.querySelector('input:checked').value === 'all';
      chooser.hidden = selector.allTables;
      update();
    });
  search.addEventListener('input', () => {
    const query = search.value.trim().toLowerCase();
    for (const row of tableList.children)
      row.hidden = query !== '' && !row.dataset.name.includes(query);
  });
  const setVisible = (checked) => {
    for (const row of tableList.children)
      if (!row.hidden)
        row.querySelector('input').checked = checked;
    update();
  };
  selectAll.addEventListener('click', () => setVisible(true));
  selectNone.addEventListener('click', () => setVisible(false));

  selector.getSelection = () => ({
    items: [...selector.items].filter(([, checkbox]) => checkbox.checked).map(([id]) => id),
    allTables: selector.allTables,
    tables: [...selector.tables].filter(([, checkbox]) => checkbox.checked).map(([uuid]) => uuid),
    otherFiles: otherCheckbox.checked
  });

  function update() {
    const selection = selector.getSelection();
    const sumOf = (bytes) => selection.items.reduce((sum, id) => sum + (bytes?.[id] || 0), 0);
    for (const table of tables)
      sizes.get(table.uuid).textContent = formatBytes(sumOf(table.bytes));
    otherRow.querySelector('.backup-item-size').textContent = formatBytes(sumOf(other));
    onChange(selection);
  }
  update();
  return selector;
}

///////////////////////////////////////////////////////////////////////////////
// Backup

async function loadBackupItems() {
  try {
    const data = await request('/backup-items');
    if (data.scanning) {
      showMessage('backup-message', 'The table library is being scanned...', '');
      setTimeout(loadBackupItems, 2000);
      return;
    }
    showMessage('backup-message', '');
    setPlaying(data.playing);
    State.backupItems = data;
    State.backupSelector = createSelector($('backup-selector'), data.items, data.tables, data.other, updateBackupTotal);
    if (data.skippedCount > 0) {
      $('backup-skipped').hidden = false;
      $('backup-skipped-summary').textContent = `${plural(data.skippedCount, 'file')} can not be backed up`;
      $('backup-skipped-list').innerHTML = '';
      for (const line of data.skipped) {
        const item = document.createElement('li');
        item.textContent = line;
        $('backup-skipped-list').appendChild(item);
      }
    }
  } catch (error) {
    showMessage('backup-message', error.status === 404 ? 'This version of Visual Pinball can not make backups: update it.' : `Failed to list what can be backed up: ${error.message}`, 'error');
    $('backup-start').disabled = true;
  }
}

// The size of the selection, estimated from the sizes by item and by table (a file shared by tables counts once in the backup)
function updateBackupTotal(selection) {
  const data = State.backupItems;
  let bytes = 0;
  if (selection.allTables)
    bytes = data.items.filter(item => selection.items.includes(item.id)).reduce((sum, item) => sum + item.bytes, 0);
  else {
    for (const table of data.tables)
      if (selection.tables.includes(table.uuid))
        for (const id of selection.items)
          bytes += table.bytes[id] || 0;
    if (selection.otherFiles)
      for (const id of selection.items)
        bytes += data.other[id] || 0;
  }
  $('backup-total').textContent = selection.items.length === 0 ? 'Nothing selected' : `About ${formatBytes(bytes)}`;
  $('backup-start').disabled = State.playing || selection.items.length === 0 || State.backupId !== null;
}

async function startBackup() {
  const selection = State.backupSelector.getSelection();
  showMessage('backup-message', '');
  $('backup-start').disabled = true;
  try {
    const job = await postJson('/backup-prepare', selection);
    State.backupId = job.id;
    setProgress('backup', `Preparing ${plural(job.files, 'file')}...`, 0);
    pollBackup();
  } catch (error) {
    showMessage('backup-message', error.message, 'error');
    $('backup-start').disabled = false;
  }
}

async function pollBackup() {
  if (State.backupId === null)
    return;
  let status;
  try {
    status = await request(`/backup-status?id=${State.backupId}`);
  } catch (error) {
    endBackup(error.message, 'error');
    return;
  }
  if (status.state === 'preparing')
    setProgress('backup', `Checking the files: ${formatBytes(status.checked)} of ${formatBytes(status.total)}`, status.total ? status.checked / status.total : 0);
  else if (status.state === 'failed') {
    endBackup(status.error || 'The backup failed', 'error');
    return;
  } else {
    if (!State.backupDownloadStarted) {
      State.backupDownloadStarted = true;
      const link = document.createElement('a');
      link.href = `/backup-download?id=${State.backupId}`;
      document.body.appendChild(link);
      link.click();
      link.remove();
    }
    if (status.error) {
      endBackup(`The download failed: ${status.error}`, 'error');
      return;
    }
    if (status.sent >= status.size) {
      endBackup(`Backup downloaded (${formatBytes(status.size)}). Keep it on your computer: it is a zip, which the Restore section of this page reads.`, 'success');
      return;
    }
    setProgress('backup', `Downloading: ${formatBytes(status.sent)} of ${formatBytes(status.size)} (keep the headset awake)`, status.size ? status.sent / status.size : 0);
  }
  State.backupPoll = setTimeout(pollBackup, POLL_INTERVAL_MS);
}

function endBackup(message, type) {
  clearTimeout(State.backupPoll);
  State.backupId = null;
  State.backupDownloadStarted = false;
  hideProgress('backup');
  showMessage('backup-message', message, type);
  updateBackupTotal(State.backupSelector.getSelection());
}

async function cancelBackup() {
  const id = State.backupId;
  endBackup('Backup cancelled', '');
  if (id !== null)
    await fetch(`/backup-cancel?id=${id}`, { method: 'POST' }).catch(() => {});
}

///////////////////////////////////////////////////////////////////////////////
// Zip reading: the central directory, then the entries one by one (the zip may be larger than the memory of the browser)

async function readBytes(file, start, length) {
  return new DataView(await file.slice(start, start + length).arrayBuffer());
}

function getUint64(view, offset) {
  return Number(view.getBigUint64(offset, true));
}

async function readZip(file) {
  const tailLength = Math.min(file.size, 22 + 65535 + 20);
  const tail = await readBytes(file, file.size - tailLength, tailLength);
  let end = -1;
  for (let i = tail.byteLength - 22; i >= 0; i--)
    if (tail.getUint32(i, true) === 0x06054b50) {
      end = i;
      break;
    }
  if (end < 0)
    throw new Error('This file is not a zip');
  let count = tail.getUint16(end + 10, true);
  let directorySize = tail.getUint32(end + 12, true);
  let directoryOffset = tail.getUint32(end + 16, true);
  if (count === 0xFFFF || directorySize === 0xFFFFFFFF || directoryOffset === 0xFFFFFFFF) {
    if (end < 20 || tail.getUint32(end - 20, true) !== 0x07064b50)
      throw new Error('This zip is damaged (no zip64 locator)');
    const zip64 = await readBytes(file, getUint64(tail, end - 20 + 8), 56);
    if (zip64.getUint32(0, true) !== 0x06064b50)
      throw new Error('This zip is damaged (no zip64 directory)');
    count = getUint64(zip64, 32);
    directorySize = getUint64(zip64, 40);
    directoryOffset = getUint64(zip64, 48);
  }
  const directory = await readBytes(file, directoryOffset, directorySize);
  const decoder = new TextDecoder();
  const entries = [];
  let p = 0;
  for (let i = 0; i < count; i++) {
    if (p + 46 > directory.byteLength || directory.getUint32(p, true) !== 0x02014b50)
      throw new Error('This zip is damaged (central directory)');
    const method = directory.getUint16(p + 10, true);
    const time = directory.getUint16(p + 12, true), date = directory.getUint16(p + 14, true);
    let compressedSize = directory.getUint32(p + 20, true);
    let size = directory.getUint32(p + 24, true);
    const nameLength = directory.getUint16(p + 28, true), extraLength = directory.getUint16(p + 30, true), commentLength = directory.getUint16(p + 32, true);
    let offset = directory.getUint32(p + 42, true);
    const name = decoder.decode(new Uint8Array(directory.buffer, directory.byteOffset + p + 46, nameLength));
    // Local time of MS-DOS, unless the exact time is given (extended timestamp)
    let modifiedAt = Math.floor(new Date(1980 + (date >> 9), ((date >> 5) & 15) - 1, date & 31, time >> 11, (time >> 5) & 63, (time & 31) * 2).getTime() / 1000);
    for (let q = p + 46 + nameLength; q + 4 <= p + 46 + nameLength + extraLength;) {
      const id = directory.getUint16(q, true), length = directory.getUint16(q + 2, true);
      if (id === 0x0001) {
        let r = q + 4;
        if (size === 0xFFFFFFFF) { size = getUint64(directory, r); r += 8; }
        if (compressedSize === 0xFFFFFFFF) { compressedSize = getUint64(directory, r); r += 8; }
        if (offset === 0xFFFFFFFF) offset = getUint64(directory, r);
      } else if (id === 0x5455 && length >= 5 && (directory.getUint8(q + 4) & 1))
        modifiedAt = directory.getUint32(q + 5, true);
      q += 4 + length;
    }
    if (!name.endsWith('/'))
      entries.push({ name, method, size, compressedSize, offset, modifiedAt });
    p += 46 + nameLength + extraLength + commentLength;
  }
  return entries;
}

// Where the bytes of an entry start, after its local header
async function getDataOffset(file, entry) {
  const header = await readBytes(file, entry.offset, 30);
  if (header.getUint32(0, true) !== 0x04034b50)
    throw new Error(`This zip is damaged (${entry.name})`);
  return entry.offset + 30 + header.getUint16(26, true) + header.getUint16(28, true);
}

async function readEntryText(file, entry) {
  const start = await getDataOffset(file, entry);
  return new TextDecoder().decode(await file.slice(start, start + entry.size).arrayBuffer());
}

///////////////////////////////////////////////////////////////////////////////
// Restore

// The tables of an entry, from the paths the manifest gives each table: its exact path, or the closest folder
function linkEntries(entries, manifest) {
  const exact = new Map(), folders = new Map();
  manifest.tables.forEach((table, index) => {
    for (const path of table.paths || []) {
      const map = path.endsWith('/') ? folders : exact;
      if (!map.has(path))
        map.set(path, []);
      map.get(path).push(index);
    }
  });
  for (const entry of entries) {
    const slash = entry.name.indexOf('/');
    entry.item = slash > 0 ? entry.name.substring(0, slash) : '';
    const path = entry.name.substring(slash + 1); // '<root>/<path>'
    entry.global = path.startsWith('pref/') || entry.name === 'aliases/pinmame/alias.txt';
    entry.tables = exact.get(path);
    for (let end = path.lastIndexOf('/'); !entry.tables && end > 0; end = path.lastIndexOf('/', end - 1))
      entry.tables = folders.get(path.substring(0, end + 1));
    entry.tables = entry.tables || [];
  }
}

async function openBackup(file) {
  showMessage('restore-message', '');
  $('restore-report').hidden = true;
  $('restore-details').hidden = true;
  State.zip = null;
  try {
    const entries = await readZip(file);
    const manifestEntry = entries.find(entry => entry.name === MANIFEST_NAME);
    if (!manifestEntry)
      throw new Error('This zip is not a backup made by Visual Pinball (it has no backup.json)');
    if (manifestEntry.method !== 0)
      throw new Error('This backup was compressed again after it was made: use the file downloaded from this page');
    const manifest = JSON.parse(await readEntryText(file, manifestEntry));
    if (manifest.format !== MANIFEST_FORMAT)
      throw new Error('This zip is not a backup made by Visual Pinball');
    if (manifest.version > 1)
      throw new Error('This backup was made by a newer version of Visual Pinball: update the application');
    manifest.tables = manifest.tables || [];
    const files = entries.filter(entry => entry !== manifestEntry);
    linkEntries(files, manifest);
    State.zip = { file, entries: files, manifestEntry, manifest };

    const items = ITEMS.map(definition => {
      const ofItem = files.filter(entry => entry.item === definition.id);
      return { id: definition.id, files: ofItem.length, bytes: ofItem.reduce((sum, entry) => sum + entry.size, 0) };
    }).filter(item => item.files > 0);
    const tables = manifest.tables.map((table, index) => ({ uuid: String(index), name: table.name, bytes: {} }));
    const other = {};
    for (const entry of files) {
      for (const index of entry.tables)
        tables[index].bytes[entry.item] = (tables[index].bytes[entry.item] || 0) + entry.size;
      if (entry.tables.length === 0 && !entry.global)
        other[entry.item] = (other[entry.item] || 0) + entry.size;
    }
    $('restore-summary').textContent = `Made ${formatDate(manifest.createdAt)} on ${describeDevice(manifest.device)} with ${manifest.app || 'Visual Pinball'}: `
      + `${plural(files.length, 'file')}, ${formatBytes(files.reduce((sum, entry) => sum + entry.size, 0))}, ${plural(manifest.tables.length, 'table')}`;
    State.restoreSelector = createSelector($('restore-selector'), items, tables, other, updateRestoreTotal);
    $('restore-details').hidden = false;
  } catch (error) {
    showMessage('restore-message', error.message, 'error');
  }
}

function describeDevice(device) {
  return { 'steam-frame': 'a Steam Frame', macos: 'a Mac', linux: 'a Linux computer', windows: 'a Windows computer' }[device] || 'a device';
}

function getRestoreEntries(selection) {
  const tables = new Set(selection.tables.map(Number));
  return State.zip.entries.filter(entry => selection.items.includes(entry.item)
    && (selection.allTables || entry.global || (entry.tables.length === 0 ? selection.otherFiles : entry.tables.some(index => tables.has(index)))));
}

function updateRestoreTotal(selection) {
  const entries = getRestoreEntries(selection);
  $('restore-total').textContent = entries.length === 0 ? 'Nothing selected' : `${plural(entries.length, 'file')}, ${formatBytes(entries.reduce((sum, entry) => sum + entry.size, 0))}`;
  $('restore-start').disabled = State.playing || entries.length === 0 || State.restoreSession !== null;
}

function confirmRestore() {
  const selection = State.restoreSelector.getSelection();
  const list = $('restore-confirm-list');
  list.innerHTML = '';
  for (const id of selection.items) {
    const item = document.createElement('li');
    item.textContent = ITEMS.find(definition => definition.id === id).label;
    list.appendChild(item);
  }
  if (!selection.allTables) {
    const item = document.createElement('li');
    item.textContent = `For ${plural(selection.tables.length, 'table')}${selection.otherFiles ? ', and the files that belong to no table' : ''}`;
    list.appendChild(item);
  }
  $('restore-confirm-dialog').showModal();
}

async function startRestore() {
  const { file } = State.zip;
  const selected = getRestoreEntries(State.restoreSelector.getSelection());
  if (selected.some(entry => entry.method !== 0)) {
    showMessage('restore-message', 'This backup was compressed again after it was made: use the file downloaded from this page', 'error');
    return;
  }
  // The manifest goes with the entries merged into what the device has (it tells which kind of device made the backup)
  const entries = [...selected, State.zip.manifestEntry];
  const totalBytes = selected.reduce((sum, entry) => sum + entry.size, 0);
  State.restoreAbort = false;
  showMessage('restore-message', '');
  $('restore-report').hidden = true;
  $('restore-start').disabled = true;
  $('restore-file').disabled = true;
  try {
    const { session } = await postJson('/restore-begin', { files: entries.length, bytes: totalBytes });
    State.restoreSession = session;

    // The files already on the device are skipped
    setProgress('restore', 'Comparing with the files of the device...', 0);
    const toUpload = [];
    let invalid = 0, skipped = 0;
    for (let start = 0; start < entries.length; start += CHECK_BATCH) {
      const batch = entries.slice(start, start + CHECK_BATCH);
      const result = await postJson(`/restore-check?session=${session}`, { entries: batch.map(entry => ({ path: entry.name, size: entry.size, modifiedAt: entry.modifiedAt })) });
      const existing = new Set(result.existing), rejected = new Set(result.invalid);
      batch.forEach((entry, index) => {
        if (rejected.has(index))
          invalid++;
        else if (existing.has(index))
          skipped++;
        else
          toUpload.push(entry);
      });
      if (State.restoreAbort)
        throw new Error('Restore stopped');
    }

    // Uploaded a few at a time, in chunks
    const uploadBytes = toUpload.reduce((sum, entry) => sum + entry.size, 0);
    let doneBytes = 0, doneFiles = 0, next = 0;
    const report = () => setProgress('restore', `Restoring: ${formatBytes(doneBytes)} of ${formatBytes(uploadBytes)}, ${doneFiles.toLocaleString()} of ${plural(toUpload.length, 'file')} (keep the headset awake)`,
      uploadBytes ? doneBytes / uploadBytes : doneFiles / Math.max(1, toUpload.length));
    report();
    const worker = async () => {
      while (next < toUpload.length && !State.restoreAbort) {
        const entry = toUpload[next++];
        const start = await getDataOffset(file, entry);
        let offset = 0;
        do {
          const length = Math.min(UPLOAD_CHUNK_SIZE, entry.size - offset);
          const params = new URLSearchParams({ session, path: entry.name, offset, length: entry.size, modifiedAt: entry.modifiedAt });
          await request(`/restore-file?${params}`, { method: 'POST', body: file.slice(start + offset, start + offset + length) });
          offset += length;
          doneBytes += length;
          report();
        } while (offset < entry.size && !State.restoreAbort);
        doneFiles++;
      }
    };
    await Promise.all(Array.from({ length: UPLOAD_PARALLEL }, worker));
    if (State.restoreAbort)
      throw new Error('Restore stopped');

    // Then the library information, the scores and the settings are merged on the device
    setProgress('restore', 'Updating the library, the scores and the settings...', 1);
    await request(`/restore-finish?session=${session}`, { method: 'POST' });
    let status;
    for (;;) {
      await new Promise(resolve => setTimeout(resolve, POLL_INTERVAL_MS));
      status = await request(`/restore-status?session=${session}`);
      if (status.state === 'done' || status.state === 'failed')
        break;
    }
    const lines = [...status.report];
    if (skipped > 0)
      lines.push(`${plural(skipped, 'file')} already on the device`);
    if (invalid > 0)
      lines.push(`${plural(invalid, 'file')} could not be restored on this device`);
    showReport(lines);
    if (status.state === 'failed')
      showMessage('restore-message', `The restore failed: ${status.error}`, 'error');
    else
      showMessage('restore-message', 'Backup restored', 'success');
    loadBackupItems(); // What the device now holds
  } catch (error) {
    showMessage('restore-message', error.message, 'error');
    if (State.restoreSession !== null)
      await fetch(`/restore-cancel?session=${State.restoreSession}`, { method: 'POST' }).catch(() => {});
  }
  State.restoreSession = null;
  hideProgress('restore');
  $('restore-file').disabled = false;
  updateRestoreTotal(State.restoreSelector.getSelection());
}

function showReport(lines) {
  const list = $('restore-report');
  list.innerHTML = '';
  for (const line of lines) {
    const item = document.createElement('li');
    item.textContent = line;
    list.appendChild(item);
  }
  list.hidden = lines.length === 0;
}

///////////////////////////////////////////////////////////////////////////////
// Startup

function setPlaying(playing) {
  State.playing = playing;
  $('backup-playing').hidden = !playing;
  if (State.backupSelector)
    updateBackupTotal(State.backupSelector.getSelection());
  if (State.restoreSelector)
    updateRestoreTotal(State.restoreSelector.getSelection());
}

// Whether a table is being played, from the status the server pushes
function watchStatus() {
  const socket = new WebSocket(`${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.host}/status`);
  socket.onmessage = (event) => {
    try {
      const status = JSON.parse(event.data);
      setPlaying(Boolean(status.running && status.currentTable));
    } catch (e) {
      // Not a status
    }
  };
  socket.onclose = () => setTimeout(watchStatus, 3000);
}

async function startup() {
  const info = await ensurePaired();
  if (info === null)
    return;
  if (info.version)
    $('version-info').textContent = info.version;
  if (info.tableLibrary === false) {
    showMessage('backup-message', 'Backups are made by the Visual Pinball app on this device.', 'error');
    $('backup-start').disabled = true;
    return;
  }

  $('backup-start').addEventListener('click', startBackup);
  $('backup-cancel').addEventListener('click', cancelBackup);
  $('restore-file').addEventListener('change', (e) => {
    if (e.target.files.length > 0)
      openBackup(e.target.files[0]);
  });
  $('restore-start').addEventListener('click', confirmRestore);
  $('restore-cancel').addEventListener('click', () => { State.restoreAbort = true; });
  $('restore-confirm-cancel').addEventListener('click', () => $('restore-confirm-dialog').close());
  $('restore-confirm-form').addEventListener('submit', () => startRestore());
  // Only its buttons close the dialog: not a click outside, nor the Escape key
  $('restore-confirm-dialog').addEventListener('cancel', (e) => e.preventDefault());

  await loadBackupItems();
  watchStatus();
}

document.addEventListener('DOMContentLoaded', startup);
