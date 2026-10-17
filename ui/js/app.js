/* ==========================================================================
   Internet Download Manager (IDM 6.38) - Native Desktop Frontend Controller
   Drives the authentic Windows UI, floating status dialog, segmented connection
   visualizer, completion popup, and REST API telemetry synchronization.
   ========================================================================== */

const IDMApp = {
    apiBase: (window.location && window.location.protocol && window.location.protocol.startsWith('http')) ? '' : 'http://127.0.0.1:8080',
    downloads: [],
    selectedId: null,
    activeFilter: 'all',
    activeCategory: null,
    activeInspectId: null,
    detailsExpanded: false,
    speedLimiterActive: false,
    pollTimer: null,
    selectedQueueTaskId: null,
    currentFileInfoContext: null,
    isDarkMode: false,
    probeOriginalPath: '',
    probeSuggestedPath: '',
    notifiedCompleted: new Set(),
    notifiedFailed: new Set(),
    audioCtx: null,

    init() {
        this.bindEvents();
        try {
            const savedDark = localStorage.getItem('idm_dark_mode');
            if (savedDark === 'true') {
                this.setDarkMode(true);
            }
        } catch (e) {}
        this.fetchTasks();
        this.pollTimer = setInterval(() => {
            this.fetchTasks();
            this.checkPendingIntercepts();
        }, 1000);

        // Immediate check and long-polling event stream for 0ms reactivity
        this.checkPendingIntercepts();
        this.initEventStream();

        // Immediate check whenever window gains focus or becomes visible
        window.addEventListener('focus', () => this.checkPendingIntercepts());
        document.addEventListener('visibilitychange', () => {
            if (document.visibilityState === 'visible') {
                this.checkPendingIntercepts();
            }
        });

        this.initDialogDragging();
        this.initDropTarget();
    },

    playChime(type) {
        const soundEnabled = localStorage.getItem('idm_play_sounds') !== 'false';
        if (!soundEnabled) return;

        try {
            const AudioCtx = window.AudioContext || window.webkitAudioContext;
            if (!AudioCtx) return;
            if (!this.audioCtx) {
                this.audioCtx = new AudioCtx();
            }
            if (this.audioCtx.state === 'suspended') {
                this.audioCtx.resume();
            }
            const ctx = this.audioCtx;

            if (type === 'complete') {
                // Classic IDM Download Complete dual crystal chime
                const osc1 = ctx.createOscillator();
                const osc2 = ctx.createOscillator();
                const gain = ctx.createGain();

                osc1.type = 'sine';
                osc1.frequency.setValueAtTime(1046.5, ctx.currentTime); // C6
                osc1.frequency.exponentialRampToValueAtTime(1318.5, ctx.currentTime + 0.08); // E6

                osc2.type = 'triangle';
                osc2.frequency.setValueAtTime(2093, ctx.currentTime); // C7

                gain.gain.setValueAtTime(0.18, ctx.currentTime);
                gain.gain.exponentialRampToValueAtTime(0.0001, ctx.currentTime + 0.65);

                osc1.connect(gain);
                osc2.connect(gain);
                gain.connect(ctx.destination);

                osc1.start();
                osc2.start();
                osc1.stop(ctx.currentTime + 0.65);
                osc2.stop(ctx.currentTime + 0.65);
            } else if (type === 'error') {
                // Classic IDM Error Alert Buzzer
                const osc = ctx.createOscillator();
                const gain = ctx.createGain();
                osc.type = 'sawtooth';
                osc.frequency.setValueAtTime(220, ctx.currentTime); // A3
                osc.frequency.setValueAtTime(164.81, ctx.currentTime + 0.15); // E3

                gain.gain.setValueAtTime(0.15, ctx.currentTime);
                gain.gain.exponentialRampToValueAtTime(0.0001, ctx.currentTime + 0.45);

                osc.connect(gain);
                gain.connect(ctx.destination);
                osc.start();
                osc.stop(ctx.currentTime + 0.45);
            }
        } catch (e) {}
    },

    setDarkMode(enable) {
        this.isDarkMode = !!enable;
        document.body.classList.toggle('dark-mode', this.isDarkMode);
        try {
            localStorage.setItem('idm_dark_mode', this.isDarkMode ? 'true' : 'false');
        } catch (e) {}

        const menuCheck = document.getElementById('darkmode-menu-check');
        if (menuCheck) menuCheck.textContent = this.isDarkMode ? '✔ ' : '';

        const themeLabel = document.getElementById('theme-btn-label');
        if (themeLabel) themeLabel.textContent = this.isDarkMode ? 'Light' : 'Dark Mode';

        const themeBtn = document.getElementById('btn-theme-toggle');
        if (themeBtn) {
            themeBtn.title = this.isDarkMode ? 'Switch to Classic Windows Light Theme' : 'Switch to Modern Dark Theme';
        }
    },

    showMessageBox(text, title = 'Internet Download Manager', icon = 'ℹ️') {
        return new Promise((resolve) => {
            const overlay = document.getElementById('custom-win-messagebox');
            const titleEl = document.getElementById('msgbox-title');
            const textEl = document.getElementById('msgbox-text');
            const iconEl = document.getElementById('msgbox-icon');
            const btnOk = document.getElementById('btn-msgbox-ok');
            const btnClose = document.getElementById('btn-msgbox-close');
            
            if (!overlay) return resolve(true);

            if (titleEl) titleEl.textContent = title;
            if (textEl) textEl.innerHTML = (text || '').replace(/\n/g, '<br>');
            if (iconEl) iconEl.textContent = icon;
            overlay.classList.add('open');

            const handleClose = () => {
                overlay.classList.remove('open');
                if (btnOk) btnOk.removeEventListener('click', handleClose);
                if (btnClose) btnClose.removeEventListener('click', handleClose);
                resolve(true);
            };

            if (btnOk) {
                btnOk.addEventListener('click', handleClose);
                btnOk.focus();
            }
            if (btnClose) {
                btnClose.addEventListener('click', handleClose);
            }
        });
    },

    showAboutDialog() {
        const dlg = document.getElementById('about-dialog');
        if (dlg) {
            dlg.classList.add('open');
            document.getElementById('btn-about-ok')?.focus();
        }
    },

    closeAboutDialog() {
        const dlg = document.getElementById('about-dialog');
        if (dlg) dlg.classList.remove('open');
    },

    showRegistrationDialog() {
        const dlg = document.getElementById('registration-dialog');
        if (dlg) {
            dlg.classList.add('open');
            document.getElementById('btn-registration-close')?.focus();
        }
    },

    closeRegistrationDialog() {
        const dlg = document.getElementById('registration-dialog');
        if (dlg) dlg.classList.remove('open');
    },

    formatBytes(bytes) {
        if (!bytes || bytes <= 0) return '0 B';
        const k = 1024;
        const sizes = ['B', 'KB', 'MB', 'GB', 'TB'];
        const i = Math.floor(Math.log(bytes) / Math.log(k));
        return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
    },

    formatBytesAccurate(bytes) {
        if (!bytes || bytes <= 0) return '0 Bytes';
        return bytes.toLocaleString() + ' Bytes';
    },

    getFileIcon(ext) {
        const lower = (ext || '').toLowerCase();
        if (['.zip', '.rar', '.7z', '.tar', '.gz', '.iso', '.dat', '.bin', '.csv'].includes(lower)) {
            return `<svg class="file-type-icon" viewBox="0 0 16 16"><path d="M2 3h12v10H2V3z" fill="#FFF59D" stroke="#F57F17" stroke-width="0.8"/><line x1="8" y1="3" x2="8" y2="13" stroke="#795548" stroke-dasharray="1 1"/></svg>`;
        } else if (['.mp4', '.mkv', '.avi', '.webm', '.mov', '.flv'].includes(lower)) {
            return `<svg class="file-type-icon" viewBox="0 0 16 16"><rect x="2" y="3" width="12" height="10" rx="1" fill="#37474F"/><rect x="4" y="4" width="2" height="2" fill="#FFF"/><rect x="7" y="4" width="2" height="2" fill="#FFF"/><rect x="10" y="4" width="2" height="2" fill="#FFF"/><rect x="4" y="10" width="2" height="2" fill="#FFF"/><rect x="7" y="10" width="2" height="2" fill="#FFF"/><rect x="10" y="10" width="2" height="2" fill="#FFF"/></svg>`;
        } else if (['.mp3', '.wav', '.flac', '.aac', '.ogg'].includes(lower)) {
            return `<svg class="file-type-icon" viewBox="0 0 16 16"><circle cx="4.5" cy="11.5" r="2.5" fill="#1976D2"/><circle cx="11.5" cy="9.5" r="2.5" fill="#1976D2"/><path d="M7 11.5V4h7v5.5M7 6h7" stroke="#1565C0" stroke-width="1.4"/></svg>`;
        } else if (['.pdf', '.doc', '.docx', '.txt', '.xlsx'].includes(lower)) {
            return `<svg class="file-type-icon" viewBox="0 0 16 16"><path d="M3 2h7l4 4v8H3V2z" fill="#FFF" stroke="#78909C" stroke-width="0.8"/><path d="M10 2v4h4" fill="#CFD8DC"/><line x1="5" y1="8" x2="11" y2="8" stroke="#1E88E5"/><line x1="5" y1="11" x2="9" y2="11" stroke="#1E88E5"/></svg>`;
        } else if (['.exe', '.msi', '.apk', '.bat'].includes(lower)) {
            return `<svg class="file-type-icon" viewBox="0 0 16 16"><rect x="2" y="2" width="12" height="12" rx="1" fill="#FFF" stroke="#455A64" stroke-width="0.8"/><rect x="2" y="2" width="12" height="3" fill="#1976D2"/><circle cx="8" cy="9" r="2.5" fill="#FFA000"/></svg>`;
        }
        return `<svg class="file-type-icon" viewBox="0 0 16 16"><path d="M3 2h10v12H3V2z" fill="#FFF" stroke="#757575" stroke-width="1"/></svg>`;
    },

    async fetchTasks() {
        try {
            const res = await fetch(`${this.apiBase}/api/tasks`);
            const data = await res.json();
            const tasks = Array.isArray(data) ? data : (data.tasks || []);
            if (!Array.isArray(tasks)) return;

            this.downloads = tasks.map(t => {
                const savePath = t.save_path || t.id || '';
                const filename = savePath.substring(Math.max(savePath.lastIndexOf('/'), savePath.lastIndexOf('\\')) + 1) || t.url;
                const ext = filename.includes('.') ? filename.substring(filename.lastIndexOf('.')).toLowerCase() : '';
                
                let category = 'General';
                if (['.zip', '.rar', '.7z', '.tar', '.gz', '.iso', '.dat', '.bin', '.csv'].includes(ext)) category = 'Compressed';
                else if (['.mp4', '.mkv', '.avi', '.webm', '.mov', '.flv'].includes(ext)) category = 'Videos';
                else if (['.mp3', '.wav', '.flac', '.aac'].includes(ext)) category = 'Music';
                else if (['.pdf', '.doc', '.docx', '.txt', '.xlsx'].includes(ext)) category = 'Documents';
                else if (['.exe', '.msi', '.apk', '.bat'].includes(ext)) category = 'Programs';

                const total = t.total_bytes || 0;
                const downloaded = t.downloaded_bytes || 0;
                const pct = t.progress_pct !== undefined ? Math.min(100, Math.max(0, Math.round(t.progress_pct))) : (total > 0 ? Math.round((downloaded * 100) / total) : 0);
                const speed = t.speed_bps > 0 ? this.formatBytes(t.speed_bps) + '/sec' : (t.status === 'Downloading' ? 'Receiving...' : '');

                return {
                    id: t.id,
                    filename: filename,
                    url: t.url,
                    savePath: savePath,
                    category: category,
                    ext: ext,
                    sizeFormatted: total > 0 ? this.formatBytes(total) : (downloaded > 0 ? this.formatBytes(downloaded) : '--'),
                    totalBytes: total,
                    downloadedBytes: downloaded,
                    status: t.status || 'Downloading',
                    progress: pct,
                    speed: speed,
                    eta: t.eta || '0 sec',
                    lastTry: new Date().toLocaleDateString('en-US', { month: 'short', day: 'numeric' }),
                    lastError: t.last_error || ''
                };
            });

            this.updateCategoryCounts();
            this.renderTable();
            this.updateStatusBar();
            this.updateToolbarButtons();
            this.checkActiveStatusDialog();
            this.checkAutoCompletionPopup();
        } catch (e) {
            // LocalServer reconnecting
        }
    },

    updateCategoryCounts() {
        const counts = {
            all: this.downloads.length,
            compressed: 0,
            documents: 0,
            music: 0,
            programs: 0,
            videos: 0,
            unfinished: 0,
            finished: 0,
            queues: 0
        };

        this.downloads.forEach(t => {
            if (t.status === 'Completed') {
                counts.finished++;
            } else {
                counts.unfinished++;
                counts.queues++;
            }

            const cat = (t.category || '').toLowerCase();
            if (cat === 'compressed') counts.compressed++;
            else if (cat === 'documents') counts.documents++;
            else if (cat === 'music') counts.music++;
            else if (cat === 'programs') counts.programs++;
            else if (cat === 'videos') counts.videos++;
        });

        const setBadge = (id, count) => {
            const el = document.getElementById(id);
            if (el) el.textContent = `(${count})`;
        };

        setBadge('count-all', counts.all);
        setBadge('count-compressed', counts.compressed);
        setBadge('count-documents', counts.documents);
        setBadge('count-music', counts.music);
        setBadge('count-programs', counts.programs);
        setBadge('count-videos', counts.videos);
        setBadge('count-unfinished', counts.unfinished);
        setBadge('count-finished', counts.finished);
        setBadge('count-queues', counts.queues);
    },

    updateToolbarButtons() {
        const resumeBtn = document.getElementById('btn-resume');
        const stopBtn = document.getElementById('btn-pause');
        const deleteBtn = document.getElementById('btn-delete');
        const deleteCompletedBtn = document.getElementById('btn-delete-completed');

        const selectedTask = this.downloads.find(t => t.id === this.selectedId);
        if (selectedTask) {
            deleteBtn.classList.remove('disabled');
            if (selectedTask.status === 'Downloading') {
                stopBtn.classList.remove('disabled');
                resumeBtn.classList.add('disabled');
            } else if (selectedTask.status === 'Paused' || selectedTask.status === 'Failed' || selectedTask.status === 'Cancelled') {
                resumeBtn.classList.remove('disabled');
                stopBtn.classList.add('disabled');
            } else {
                resumeBtn.classList.add('disabled');
                stopBtn.classList.add('disabled');
            }
        } else {
            resumeBtn.classList.add('disabled');
            stopBtn.classList.add('disabled');
            deleteBtn.classList.add('disabled');
        }

        const hasCompleted = this.downloads.some(t => t.status === 'Completed');
        if (hasCompleted) {
            deleteCompletedBtn.classList.remove('disabled');
        } else {
            deleteCompletedBtn.classList.add('disabled');
        }
    },

    renderTable() {
        const tbody = document.getElementById('download-table-body');
        if (!tbody) return;

        tbody.innerHTML = '';

        const filtered = this.downloads.filter(task => {
            if (this.activeFilter === 'unfinished') return task.status !== 'Completed';
            if (this.activeFilter === 'finished') return task.status === 'Completed';
            if (this.activeFilter === 'category') return task.category === this.activeCategory;
            return true;
        });

        if (filtered.length === 0) {
            tbody.innerHTML = `
                <tr>
                    <td colspan="9" style="text-align: center; color: var(--win-text-muted); padding: 32px; font-size: 11.5px;">
                        No download tasks in queue. Click "Add URL" to start downloading!
                    </td>
                </tr>`;
            return;
        }

        filtered.forEach(task => {
            const tr = document.createElement('tr');
            if (task.id === this.selectedId) tr.className = 'selected';

            let statusHtml = '';
            if (task.status === 'Downloading') {
                statusHtml = `
                    <div class="tbl-progress-box" title="${task.progress}% (${task.speed})">
                        <div class="tbl-progress-bar">
                            <div class="fill" style="width: ${task.progress}%;"></div>
                        </div>
                        <span class="tbl-progress-text">${task.progress}%</span>
                    </div>`;
            } else if (task.status === 'Paused') {
                statusHtml = `
                    <div class="tbl-progress-box" title="Paused (${task.progress}%)">
                        <div class="tbl-progress-bar">
                            <div class="fill paused" style="width: ${task.progress}%;"></div>
                        </div>
                        <span class="tbl-progress-text" style="color: #1976D2;">${task.progress}%</span>
                    </div>`;
            } else if (task.status === 'Cancelled') {
                statusHtml = `
                    <div class="tbl-progress-box" title="Cancelled (${task.progress}%)">
                        <div class="tbl-progress-bar">
                            <div class="fill" style="width: ${task.progress}%; background: #D32F2F;"></div>
                        </div>
                        <span class="tbl-progress-text" style="color: #D32F2F;">Cancelled</span>
                    </div>`;
            } else if (task.status === 'Completed') {
                statusHtml = `<span class="tbl-status-badge complete">Complete</span>`;
            } else if (task.status === 'Failed') {
                if (task.lastError && (task.lastError.includes('Threat') || task.lastError.includes('Security') || task.lastError.includes('Virus') || task.lastError.includes('Malware') || task.lastError.includes('SmartScreen'))) {
                    statusHtml = `<span class="tbl-status-badge error" style="background:#FFCDD2; color:#B71C1C; font-weight:bold; cursor:pointer;" title="${task.lastError}">⚠️ Threat Detected</span>`;
                } else if (task.lastError && task.lastError.includes('404')) {
                    statusHtml = `<span class="tbl-status-badge error" style="background:#FFEBEE; color:#C62828;" title="${task.lastError}">404 Not Found</span>`;
                } else if (task.lastError) {
                    statusHtml = `<span class="tbl-status-badge error" title="${task.lastError}">Error (${task.lastError.length > 20 ? task.lastError.substring(0, 18) + '...' : task.lastError})</span>`;
                } else {
                    statusHtml = `<span class="tbl-status-badge error">Error</span>`;
                }
            } else {
                statusHtml = `<span class="tbl-status-badge">${task.status}</span>`;
            }

            const queueIcon = `<span style="display:inline-flex; align-items:center; justify-content:center;" title="Main Queue"><svg width="14" height="13" viewBox="0 0 16 16"><path d="M1 3h5l2 2h7v9H1V3z" fill="#FFE082" stroke="#F57F17" stroke-width="0.9"/></svg></span>`;

            tr.innerHTML = `
                <td style="text-align: center;">${this.getFileIcon(task.ext)}</td>
                <td style="font-weight: 500;" title="${task.filename}">${task.filename}</td>
                <td style="text-align: center;">${queueIcon}</td>
                <td style="text-align: right;">${task.sizeFormatted}</td>
                <td>${statusHtml}</td>
                <td>${task.eta}</td>
                <td style="text-align: right;">${task.speed}</td>
                <td>${task.lastTry}</td>
                <td style="color: var(--win-text-muted);">${task.category}</td>
            `;

            tr.addEventListener('click', () => {
                this.selectedId = task.id;
                this.renderTable();
                this.updateToolbarButtons();
            });

            tr.addEventListener('dblclick', () => {
                if (task.status === 'Completed') {
                    this.openCompleteDialog(task);
                } else {
                    if (!this.openIndependentStatusWindow(task)) {
                        this.openStatusDialog(task);
                    }
                }
            });

            tr.addEventListener('contextmenu', (e) => {
                e.preventDefault();
                this.selectedId = task.id;
                this.renderTable();
                this.updateToolbarButtons();
                this.openContextMenu(e.clientX, e.clientY, task);
            });

            tbody.appendChild(tr);
        });
    },

    updateStatusBar() {
        const totalCount = this.downloads.length;
        const activeCount = this.downloads.filter(t => t.status === 'Downloading').length;
        document.getElementById('status-task-count').textContent = `${totalCount} downloads`;
        document.getElementById('status-speed-info').textContent = activeCount > 0 ? `Queue: Active (${activeCount} downloading)` : 'Queue: Idle';
    },

    /* ==========================================================================
       Floating Download Status Dialog (Images 1 & 2)
       ========================================================================== */
    openIndependentStatusWindow(task) {
        if (!task || !task.id) return false;
        this.activeInspectId = task.id;
        const w = 530;
        const h = 360;
        const left = Math.max(10, Math.round((window.screen.width - w) / 2));
        const top = Math.max(10, Math.round((window.screen.height - h) / 2));
        try {
            const popup = window.open(
                `download_status.html?id=${encodeURIComponent(task.id)}`,
                `IDM_Status_${String(task.id).replace(/[^a-zA-Z0-9]/g, '_')}`,
                `width=${w},height=${h},left=${left},top=${top},menubar=no,toolbar=no,location=no,status=no,resizable=yes`
            );
            if (popup && !popup.closed) {
                popup.focus();
                this.closeStatusDialog();
                return true;
            }
        } catch (e) {}
        return false;
    },

    openStatusDialog(task) {
        if (this.openIndependentStatusWindow(task)) {
            return;
        }
        this.activeInspectId = task.id;
        document.getElementById('download-status-dialog').classList.add('open');
        this.updateStatusDialogUI(task);
        // Automatically expand connection details so thread progress is immediately visible
        if (task.status === 'Downloading') {
            this.detailsExpanded = true;
            document.getElementById('connection-details-panel').classList.add('open');
            document.getElementById('btn-toggle-details').textContent = '<< Hide details';
        }
        this.fetchChunkStreams(task.id);
    },

    closeStatusDialog() {
        document.getElementById('download-status-dialog').classList.remove('open');
        this.activeInspectId = null;
    },

    checkActiveStatusDialog() {
        if (!this.activeInspectId) return;
        const task = this.downloads.find(t => t.id === this.activeInspectId || this.isSameTask(t.id, this.activeInspectId));
        if (task) {
            this.updateStatusDialogUI(task);
            this.fetchChunkStreams(task.id);
        }
    },

    updateStatusDialogUI(task) {
        document.getElementById('dlg-title-text').textContent = `${task.progress}% ${task.filename}`;
        const urlEl = document.getElementById('dlg-url');
        if (urlEl) {
            urlEl.textContent = task.url;
            urlEl.title = task.url;
        }

        const secAlert = document.getElementById('dlg-sec-alert');
        const secMsg = document.getElementById('dlg-sec-alert-msg');
        const secBtn = document.getElementById('btn-dlg-sec-override');
        const pauseBtn = document.getElementById('btn-dlg-pause');
        const cancelBtn = document.getElementById('btn-dlg-cancel');

        if (task.lastError && (task.lastError.includes('SmartScreen') || task.lastError.includes('security') || task.lastError.includes('Blocked'))) {
            if (secAlert) {
                secAlert.style.display = 'flex';
                if (secMsg) secMsg.textContent = task.lastError;
                if (secBtn) {
                    secBtn.onclick = async () => {
                        await fetch(`${this.apiBase}/api/av_override`, {
                            method: 'POST',
                            headers: { 'Content-Type': 'application/json' },
                            body: JSON.stringify({ id: task.id, override: true })
                        });
                        await this.resumeTask(task.id);
                        secAlert.style.display = 'none';
                    };
                }
            }
            document.getElementById('dlg-status').innerHTML = '<span style="color: #D32F2F; font-weight: bold;">Blocked by Windows SmartScreen</span>';
            pauseBtn.style.display = 'none';
        } else {
            if (secAlert) secAlert.style.display = 'none';
            document.getElementById('dlg-status').textContent = task.status === 'Downloading' ? 'Receiving data...' : task.status;
            if (task.status === 'Completed') {
                pauseBtn.style.display = 'none';
                if (cancelBtn) cancelBtn.textContent = 'Close';
            } else if (task.status === 'Cancelled') {
                pauseBtn.style.display = 'inline-block';
                pauseBtn.textContent = 'Restart';
                if (cancelBtn) cancelBtn.textContent = 'Close';
            } else if (task.status === 'Paused') {
                pauseBtn.style.display = 'inline-block';
                pauseBtn.textContent = 'Resume';
                if (cancelBtn) cancelBtn.textContent = 'Cancel';
            } else if (task.status === 'NotStarted') {
                pauseBtn.style.display = 'inline-block';
                pauseBtn.textContent = 'Start';
                if (cancelBtn) cancelBtn.textContent = 'Cancel';
            } else {
                pauseBtn.style.display = 'inline-block';
                pauseBtn.textContent = 'Pause';
                if (cancelBtn) cancelBtn.textContent = 'Cancel';
            }
        }

        document.getElementById('dlg-file-size').textContent = task.sizeFormatted;
        document.getElementById('dlg-downloaded').textContent = `${this.formatBytes(task.downloadedBytes)} ( ${task.progress} % )`;
        document.getElementById('dlg-transfer-rate').textContent = task.speed || '0 KB/sec';
        document.getElementById('dlg-time-left').textContent = task.eta;
        document.getElementById('dlg-progress-fill').style.width = `${task.progress}%`;

        const resumeCapEl = document.getElementById('dlg-resume-cap');
        if (resumeCapEl) {
            const hasRanges = (task.supports_ranges !== false);
            resumeCapEl.textContent = hasRanges ? 'Yes' : 'No';
            resumeCapEl.style.color = hasRanges ? '#008000' : '#888888';
        }
    },

    async fetchChunkStreams(taskId) {
        try {
            const res = await fetch(`${this.apiBase}/api/chunks?id=${encodeURIComponent(taskId)}`);
            const data = await res.json();
            const chunks = data.chunks || [];
            this.renderSegmentedBar(chunks);
            this.renderThreadTable(chunks);
        } catch (e) {}
    },

    renderSegmentedBar(chunks) {
        const track = document.getElementById('segmented-progress-track');
        if (!track) return;
        track.innerHTML = '';

        if (chunks.length === 0) {
            track.innerHTML = '<div style="width: 100%; height: 100%; background: #E0E0E0;"></div>';
            return;
        }

        const maxByte = Math.max(...chunks.map(c => c.end_byte || 0), 1);

        chunks.forEach((c) => {
            const segLeft = ((c.start_byte / maxByte) * 100).toFixed(2);
            const segWidth = (((c.end_byte - c.start_byte + 1) / maxByte) * 100).toFixed(2);
            const totalChunkBytes = Math.max(1, c.end_byte - c.start_byte + 1);
            const chunkProgressPct = Math.min(100, Math.max(0, ((c.downloaded_bytes / totalChunkBytes) * 100))).toFixed(1);

            const segDiv = document.createElement('div');
            segDiv.className = 'segment-item';
            segDiv.style.left = `${segLeft}%`;
            segDiv.style.width = `${segWidth}%`;

            segDiv.innerHTML = `<div class="segment-progress" style="width: ${chunkProgressPct}%;"></div>`;
            track.appendChild(segDiv);
        });
    },

    renderThreadTable(chunks) {
        const tbody = document.getElementById('thread-table-body');
        if (!tbody) return;
        tbody.innerHTML = '';

        if (chunks.length === 0) {
            tbody.innerHTML = '<tr><td colspan="4" style="text-align: center; color: #888; padding: 10px;">Waiting for connection...</td></tr>';
            return;
        }

        chunks.forEach((c, idx) => {
            const tr = document.createElement('tr');
            const totalChunkBytes = Math.max(1, (c.end_byte && c.end_byte >= c.start_byte) ? (c.end_byte - c.start_byte + 1) : (c.downloaded_bytes || 1));
            const pct = Math.min(100, Math.max(0, Math.round(((c.downloaded_bytes || 0) / totalChunkBytes) * 100)));
            const infoText = c.status === 'Done' ? 'Complete' : (c.status === 'InProgress' ? 'Receiving data...' : (c.status === 'Failed' ? 'Retrying...' : 'Send GET...'));
            tr.innerHTML = `
                <td style="font-weight: 600; text-align: center;">${idx + 1}</td>
                <td>${this.formatBytes(c.downloaded_bytes)}</td>
                <td>
                    <div style="display: flex; align-items: center; gap: 6px;">
                        <div style="flex: 1; height: 11px; background: #FFF; border: 1px inset #7F9DB9; border-radius: 1px; overflow: hidden; position: relative;">
                            <div style="width: ${pct}%; height: 100%; background: linear-gradient(180deg, #6ee474 0%, #30b836 50%, #209626 100%);"></div>
                        </div>
                        <span style="font-size: 10.5px; font-weight: 600; width: 34px; text-align: right;">${pct}%</span>
                    </div>
                </td>
                <td style="color: ${c.status === 'Done' ? '#008000' : '#0000FF'}; font-weight: 500;">${infoText}</td>
            `;
            tbody.appendChild(tr);
        });
    },

    toggleDetails() {
        this.detailsExpanded = !this.detailsExpanded;
        const panel = document.getElementById('connection-details-panel');
        const btn = document.getElementById('btn-toggle-details');
        if (this.detailsExpanded) {
            panel.classList.add('open');
            btn.textContent = '<< Hide details';
        } else {
            panel.classList.remove('open');
            btn.textContent = 'Show details >>';
        }
    },

    isSameTask(id1, id2) {
        if (!id1 || !id2) return false;
        if (id1 === id2) return true;
        const cleanA = String(id1).replace(/\\/g, '/').toLowerCase();
        const cleanB = String(id2).replace(/\\/g, '/').toLowerCase();
        return cleanA === cleanB || cleanA.endsWith('/' + cleanB) || cleanB.endsWith('/' + cleanA);
    },

    /* ==========================================================================
       "Download Complete" Popup Dialog (Image 4)
       ========================================================================== */
    checkAutoCompletionPopup() {
        this.downloads.forEach(task => {
            if (task.status === 'Completed' && !this.notifiedCompleted.has(task.id)) {
                this.notifiedCompleted.add(task.id);
                this.playChime('complete');
                // Phase 7 Task 22: Fire native Windows balloon / tray notification
                fetch(`${this.apiBase}/api/notify`, {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({
                        title: 'Download complete',
                        message: `${task.filename} (${task.sizeFormatted})`
                    })
                }).catch(() => {});

                // Close the in-progress status dialog immediately if open for this task inside main window
                if (this.activeInspectId && (
                    this.activeInspectId === task.id ||
                    this.isSameTask(this.activeInspectId, task.id) ||
                    this.isSameTask(this.activeInspectId, task.savePath) ||
                    this.isSameTask(this.activeInspectId, task.url) ||
                    this.isSameTask(this.activeInspectId, task.filename)
                )) {
                    this.closeStatusDialog();
                }

                // If standalone download_status.html is active for this task, it already displays completion!
                let hasActiveStandalone = false;
                try {
                    const lastPing = parseInt(localStorage.getItem('idm_active_status_' + task.id) || '0', 10);
                    if (Date.now() - lastPing < 3000) {
                        hasActiveStandalone = true;
                    }
                } catch(e){}

                if (hasActiveStandalone) {
                    return;
                }

                // Transition to the Download Complete Dialog unless user checked "Don't show this dialog again"
                const dontShow = document.getElementById('chk-dont-show-complete')?.checked;
                if (!dontShow) {
                    this.openCompleteDialog(task);
                }
            } else if (task.status === 'Failed' && !this.notifiedFailed.has(task.id)) {
                this.notifiedFailed.add(task.id);
                this.playChime('error');
            }
        });
    },

    openCompleteDialog(task) {
        const dialog = document.getElementById('download-complete-dialog');
        const summary = document.getElementById('complete-summary-text');
        const urlField = document.getElementById('complete-url-field');
        const pathField = document.getElementById('complete-path-field');

        summary.textContent = `Downloaded ${task.sizeFormatted} (${this.formatBytesAccurate(task.totalBytes || task.downloadedBytes)})`;
        urlField.value = task.url;
        pathField.value = task.savePath;
        dialog.classList.add('open');

        document.getElementById('btn-complete-open').onclick = () => {
            this.openFile(task.savePath);
            this.closeCompleteDialog();
        };
        document.getElementById('btn-complete-open-with').onclick = () => {
            this.openFile(task.savePath);
            this.closeCompleteDialog();
        };
        document.getElementById('btn-complete-open-folder').onclick = () => {
            this.openFolder(task.savePath);
        };
        document.getElementById('btn-complete-close').onclick = () => this.closeCompleteDialog();
        const closeCross = document.getElementById('btn-close-complete-dialog');
        if (closeCross) closeCross.onclick = () => this.closeCompleteDialog();
    },

    closeCompleteDialog() {
        document.getElementById('download-complete-dialog').classList.remove('open');
    },

    /* ==========================================================================
       Native File Actions (/api/open_file, /api/open_folder)
       ========================================================================== */
    async openFile(path) {
        try {
            await fetch(`${this.apiBase}/api/open_file`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ path: path })
            });
        } catch (e) {}
    },

    async openFolder(path) {
        try {
            await fetch(`${this.apiBase}/api/open_folder`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ path: path })
            });
        } catch (e) {}
    },

    /* ==========================================================================
       Right-Click Context Menu
       ========================================================================== */
    openContextMenu(x, y, task) {
        const menu = document.getElementById('task-context-menu');
        menu.style.left = `${Math.min(window.innerWidth - 180, x)}px`;
        menu.style.top = `${Math.min(window.innerHeight - 240, y)}px`;
        menu.classList.add('open');

        document.getElementById('ctx-open-file').onclick = () => { this.openFile(task.savePath); this.closeContextMenu(); };
        document.getElementById('ctx-open-folder').onclick = () => { this.openFolder(task.savePath); this.closeContextMenu(); };
        document.getElementById('ctx-resume').onclick = () => { this.resumeTask(task.id); this.closeContextMenu(); };
        document.getElementById('ctx-pause').onclick = () => { this.pauseTask(task.id); this.closeContextMenu(); };
        document.getElementById('ctx-delete').onclick = () => { this.promptDeleteTask(task.id); this.closeContextMenu(); };
        const ctxPreview = document.getElementById('ctx-preview-stream');
        if (ctxPreview) {
            ctxPreview.onclick = () => {
                this.openStreamPreview(task);
                this.closeContextMenu();
            };
        }
        document.getElementById('ctx-copy-link').onclick = () => {
            navigator.clipboard.writeText(task.url);
            this.closeContextMenu();
        };
        const ctxProp = document.getElementById('ctx-properties');
        if (ctxProp) {
            ctxProp.onclick = () => { this.openStatusDialog(task); this.closeContextMenu(); };
        }
    },

    openStreamPreview(task) {
        const dialog = document.getElementById('stream-preview-dialog');
        const titleEl = document.getElementById('preview-dialog-title');
        const video = document.getElementById('preview-video-player');
        const statusEl = document.getElementById('preview-video-status');

        if (!dialog || !video) return;

        titleEl.textContent = `Stream Preview: ${task.filename}`;
        statusEl.textContent = `Streaming HTTP 206 Partial Content: ${task.filename}`;

        const previewUrl = `${this.apiBase}/api/preview?path=${encodeURIComponent(task.savePath)}&id=${encodeURIComponent(task.id)}`;
        video.src = previewUrl;
        video.load();
        video.play().catch(() => {});

        dialog.classList.add('open');

        const closePreview = () => {
            video.pause();
            video.src = '';
            dialog.classList.remove('open');
        };

        const closeBtn = document.getElementById('btn-close-preview-dialog');
        const bottomBtn = document.getElementById('btn-close-preview-btn');
        if (closeBtn) closeBtn.onclick = closePreview;
        if (bottomBtn) bottomBtn.onclick = closePreview;
    },

    closeContextMenu() {
        document.getElementById('task-context-menu').classList.remove('open');
    },

    /* ==========================================================================
       Toolbar & Menu Actions
       ========================================================================== */
    async resumeTask(id) {
        try {
            await fetch(`${this.apiBase}/api/resume`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ id: id })
            });
            await this.fetchTasks();
        } catch (e) {}
    },

    async pauseTask(id) {
        try {
            await fetch(`${this.apiBase}/api/pause`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ id: id })
            });
            await this.fetchTasks();
        } catch (e) {}
    },

    async deleteTask(id, deleteDisk = false) {
        try {
            await fetch(`${this.apiBase}/api/delete`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ id: id, delete_file: deleteDisk })
            });
            this.selectedId = null;
            if (this.notifiedCompleted) this.notifiedCompleted.delete(id);
            await this.fetchTasks();
        } catch (e) {}
    },

    async cancelTask(id) {
        if (!id) return;
        try {
            await fetch(`${this.apiBase}/api/cancel`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ id: id })
            });
            await this.fetchTasks();
        } catch (e) {}
    },

    promptDeleteTask(id) {
        if (!id) return;
        this.pendingDeleteId = id;
        const task = this.downloads.find(t => t.id === id);
        const nameEl = document.getElementById('delete-dialog-filename');
        if (nameEl && task) {
            nameEl.textContent = `Are you sure you want to delete "${task.filename}" from the list?`;
        } else if (nameEl) {
            nameEl.textContent = "Are you sure you want to delete this download from the list?";
        }
        const chkDisk = document.getElementById('chk-delete-disk-file');
        if (chkDisk) chkDisk.checked = false;
        const dlg = document.getElementById('delete-confirm-dialog');
        if (dlg) dlg.classList.add('open');
        document.getElementById('btn-confirm-delete')?.focus();
    },

    closeDeleteDialog() {
        const dlg = document.getElementById('delete-confirm-dialog');
        if (dlg) dlg.classList.remove('open');
        this.pendingDeleteId = null;
    },

    async stopAll() {
        for (const t of this.downloads) {
            if (t.status === 'Downloading') {
                await this.pauseTask(t.id);
            }
        }
    },

    async deleteCompleted() {
        const completed = this.downloads.filter(t => t.status === 'Completed');
        for (const t of completed) {
            await this.deleteTask(t.id, false);
        }
    },

    /* ==========================================================================
       Download File Info & Interception Workflow (Classic IDM)
       ========================================================================== */
    openIndependentFileInfoWindow(params = {}) {
        const url = (params.url || '').trim();
        const filename = (params.filename || '').trim();
        const category = (params.category || 'General').trim();
        const interceptId = (params.id || '').trim();
        const w = 520;
        const h = 290;
        const left = Math.max(10, Math.round((window.screen.width - w) / 2));
        const top = Math.max(10, Math.round((window.screen.height - h) / 2));
        try {
            const popup = window.open(
                `download_info.html?url=${encodeURIComponent(url)}&filename=${encodeURIComponent(filename)}&category=${encodeURIComponent(category)}&intercept_id=${encodeURIComponent(interceptId)}`,
                `IDM_Info_${Date.now()}`,
                `width=${w},height=${h},left=${left},top=${top},menubar=no,toolbar=no,location=no,status=no,resizable=yes`
            );
            if (popup && !popup.closed) {
                popup.focus();
                return true;
            }
        } catch (e) {}
        return false;
    },

    async openFileInfoDialog(params = {}) {
        if (this.openIndependentFileInfoWindow(params)) {
            return;
        }
        this.currentFileInfoContext = params || {};
        const url = (params.url || '').trim();
        
        document.getElementById('info-url').value = url;
        document.getElementById('info-file-size').textContent = url ? 'Probing server...' : '--';
        document.getElementById('info-description').value = params.page_title || '';
        
        let initialCat = params.category || 'General';
        document.getElementById('info-category').value = initialCat;
        document.getElementById('info-cat-name').textContent = initialCat;

        let saveName = params.filename || '';
        if (!saveName && url) {
            try {
                const u = new URL(url);
                saveName = u.pathname.split('/').filter(Boolean).pop() || 'download.bin';
                if (saveName.length > 60) saveName = saveName.substring(0, 60);
            } catch (e) {
                saveName = 'download.bin';
            }
        }
        if (!saveName) saveName = 'download.bin';

        const defaultBase = document.getElementById('opt-save-dir')?.value || 'C:\\Downloads';
        const targetDir = initialCat && initialCat !== 'General' ? `${defaultBase}\\${initialCat}` : defaultBase;
        document.getElementById('info-save-as').value = `${targetDir}\\${saveName}`;

        const dupBanner = document.getElementById('info-duplicate-banner');
        if (dupBanner) dupBanner.style.display = 'none';

        document.getElementById('download-file-info-dialog').classList.add('open');
        document.getElementById('btn-info-start-download')?.focus();

        if (url) {
            await this.probeUrl(url, params.cookies, params.referer, params.user_agent);
        }
    },

    async probeUrl(url, cookies = '', referer = '', userAgent = '') {
        if (!url || !url.startsWith('http')) return;
        try {
            const res = await fetch(`${this.apiBase}/api/probe_url`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({
                    url: url,
                    cookies: cookies || '',
                    referer: referer || '',
                    user_agent: userAgent || ''
                })
            });
            const data = await res.json();
            if (data.status === 'SUCCESS') {
                if (data.file_size_formatted) {
                    document.getElementById('info-file-size').textContent = data.file_size_formatted;
                }
                if (data.category) {
                    document.getElementById('info-category').value = data.category;
                    document.getElementById('info-cat-name').textContent = data.category;
                }

                const currentSave = document.getElementById('info-save-as').value;
                let dir = 'C:\\Downloads\\';
                if (currentSave.includes('\\')) {
                    dir = currentSave.substring(0, currentSave.lastIndexOf('\\') + 1);
                } else if (data.category && data.category !== 'General') {
                    dir = `C:\\Downloads\\${data.category}\\`;
                }

                const fname = data.file_name || 'download.bin';
                const originalPath = dir + fname;
                this.probeOriginalPath = originalPath;
                this.probeSuggestedPath = data.suggested_path || originalPath;

                const dupBanner = document.getElementById('info-duplicate-banner');
                const dupText = document.getElementById('info-duplicate-text');
                const chkOverwrite = document.getElementById('chk-info-overwrite');

                if (data.file_exists) {
                    if (dupBanner) dupBanner.style.display = 'block';
                    if (chkOverwrite) chkOverwrite.checked = false;
                    const suggestedName = this.probeSuggestedPath.split('\\').pop() || fname;
                    if (dupText) {
                        dupText.textContent = `A file named "${fname}" already exists in this folder. It will be saved as "${suggestedName}" to avoid overwriting, or check overwrite below.`;
                    }
                    document.getElementById('info-save-as').value = this.probeSuggestedPath;
                } else {
                    if (dupBanner) dupBanner.style.display = 'none';
                    document.getElementById('info-save-as').value = originalPath;
                }

                if (this.currentFileInfoContext) {
                    this.currentFileInfoContext.probed = data;
                }
            } else {
                document.getElementById('info-file-size').textContent = 'Unknown (direct stream)';
            }
        } catch (e) {
            document.getElementById('info-file-size').textContent = 'Unknown';
        }
    },

    async submitFileInfoDownload(startNow) {
        const url = document.getElementById('info-url').value.trim();
        const savePath = document.getElementById('info-save-as').value.trim();
        const category = document.getElementById('info-category').value;
        const description = document.getElementById('info-description').value.trim();
        const overwrite = document.getElementById('chk-info-overwrite')?.checked || false;

        if (!url) {
            await this.showMessageBox('Please enter a valid download URL.', 'Invalid URL', '⚠️');
            return;
        }

        try {
            const res = await fetch(`${this.apiBase}/api/add_download`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({
                    url: url,
                    save_path: savePath,
                    category: category,
                    description: description,
                    threads: 8,
                    overwrite: overwrite,
                    cookies: this.currentFileInfoContext?.cookies || '',
                    referer: this.currentFileInfoContext?.referer || '',
                    user_agent: this.currentFileInfoContext?.user_agent || '',
                    start_now: startNow,
                    confirmed: true
                })
            });
            const data = await res.json();

            document.getElementById('download-file-info-dialog').classList.remove('open');
            await this.fetchTasks();

            if (startNow) {
                const found = this.downloads.find(t => t.url === url || t.savePath === savePath);
                const taskToOpen = found || {
                    id: data.id || savePath,
                    filename: savePath.substring(Math.max(savePath.lastIndexOf('/'), savePath.lastIndexOf('\\')) + 1) || url,
                    url: url,
                    savePath: savePath,
                    category: category,
                    sizeFormatted: document.getElementById('info-file-size').textContent,
                    totalBytes: this.currentFileInfoContext?.probed?.total_size || 0,
                    downloadedBytes: 0,
                    status: 'Downloading',
                    progress: 0,
                    speed: 'Connecting...',
                    eta: '--'
                };
                // Open as independent desktop window like real IDM!
                if (!this.openIndependentStatusWindow(taskToOpen)) {
                    this.openStatusDialog(taskToOpen);
                }
            }
        } catch (err) {
            await this.showMessageBox('Failed to add download: ' + err.message, 'Error', '❌');
        }
    },

    async checkPendingIntercepts() {
        if (this.isCheckingIntercepts) return;
        this.isCheckingIntercepts = true;
        try {
            const res = await fetch(`${this.apiBase}/api/pending_intercepts`);
            const data = await res.json();
            if (data.status === 'SUCCESS' && Array.isArray(data.intercepts) && data.intercepts.length > 0) {
                const fileInfoDlg = document.getElementById('download-file-info-dialog');
                if (!fileInfoDlg.classList.contains('open')) {
                    const item = data.intercepts[0];
                    await fetch(`${this.apiBase}/api/clear_intercept`, {
                        method: 'POST',
                        headers: { 'Content-Type': 'application/json' },
                        body: JSON.stringify({ id: item.id })
                    });
                    this.openFileInfoDialog(item);
                }
            }
        } catch (e) {
        } finally {
            this.isCheckingIntercepts = false;
        }
    },

    async initEventStream() {
        let since = 0;
        const pollEvents = async () => {
            try {
                const res = await fetch(`${this.apiBase}/api/events?since=${since}`);
                if (res.ok) {
                    const data = await res.json();
                    if (data.status === 'SUCCESS') {
                        if (data.event_counter !== undefined) {
                            if (since !== 0 && data.event_counter !== since) {
                                await this.checkPendingIntercepts();
                            }
                            since = data.event_counter;
                        }
                        if (data.pending_intercepts_count > 0) {
                            await this.checkPendingIntercepts();
                        }
                    }
                }
            } catch (err) {
                await new Promise(r => setTimeout(r, 2000));
            }
            setTimeout(pollEvents, 50);
        };
        pollEvents();
    },

    async toggleSpeedLimiter() {
        this.speedLimiterActive = !this.speedLimiterActive;
        const label = document.getElementById('speed-limiter-label');
        if (label) {
            label.textContent = this.speedLimiterActive ? 'Limit: ON' : 'Speed Limit';
        }
        try {
            await fetch(`${this.apiBase}/api/speed_limiter`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({
                    id: '',
                    speed_limit_bps: this.speedLimiterActive ? (500 * 1024) : 0
                })
            });
        } catch (e) {}
    },

    bindEvents() {
        // Document clicks close context menu
        document.addEventListener('click', (e) => {
            if (!e.target.closest('#task-context-menu')) {
                this.closeContextMenu();
            }
        });

        // Toolbar Add URL Button -> Prompt URL or open Download File Info
        document.getElementById('btn-add-url').addEventListener('click', () => {
            document.getElementById('add-url-dialog').classList.add('open');
            document.getElementById('input-new-url').focus();
        });
        document.getElementById('btn-close-add-url').addEventListener('click', () => {
            document.getElementById('add-url-dialog').classList.remove('open');
        });
        document.getElementById('btn-cancel-add-url').addEventListener('click', () => {
            document.getElementById('add-url-dialog').classList.remove('open');
        });
        document.getElementById('btn-submit-add-url').addEventListener('click', () => {
            const entered = document.getElementById('input-new-url').value.trim();
            if (entered) {
                document.getElementById('add-url-dialog').classList.remove('open');
                document.getElementById('input-new-url').value = '';
                this.openFileInfoDialog({ url: entered });
            }
        });
        document.getElementById('input-new-url').addEventListener('keydown', (e) => {
            if (e.key === 'Enter') {
                document.getElementById('btn-submit-add-url').click();
            }
        });

        // Download File Info Dialog Event Listeners
        document.getElementById('btn-info-start-download').addEventListener('click', () => {
            this.submitFileInfoDownload(true);
        });
        document.getElementById('btn-info-download-later').addEventListener('click', () => {
            this.submitFileInfoDownload(false);
        });
        document.getElementById('btn-info-cancel').addEventListener('click', () => {
            document.getElementById('download-file-info-dialog').classList.remove('open');
        });
        document.getElementById('btn-close-file-info').addEventListener('click', () => {
            document.getElementById('download-file-info-dialog').classList.remove('open');
        });
        document.getElementById('info-category').addEventListener('change', (e) => {
            const newCat = e.target.value;
            document.getElementById('info-cat-name').textContent = newCat;
            const currentSave = document.getElementById('info-save-as').value;
            const fname = currentSave ? (currentSave.split('\\').pop() || 'download.bin') : 'download.bin';
            const baseDir = document.getElementById('opt-save-dir')?.value || 'C:\\Downloads';
            const targetDir = newCat && newCat !== 'General' ? `${baseDir}\\${newCat}` : baseDir;
            document.getElementById('info-save-as').value = `${targetDir}\\${fname}`;
        });

        document.getElementById('chk-info-overwrite')?.addEventListener('change', (e) => {
            const saveInput = document.getElementById('info-save-as');
            if (e.target.checked && this.probeOriginalPath) {
                saveInput.value = this.probeOriginalPath;
            } else if (!e.target.checked && this.probeSuggestedPath) {
                saveInput.value = this.probeSuggestedPath;
            }
        });

        // Browse destination folder using Native Windows Folder Dialog
        document.getElementById('btn-browse-save-as').addEventListener('click', async () => {
            const cur = document.getElementById('info-save-as').value;
            let initialDir = 'C:\\Downloads';
            if (cur && cur.includes('\\')) {
                initialDir = cur.substring(0, cur.lastIndexOf('\\'));
            }
            try {
                const res = await fetch(`${this.apiBase}/api/browse_folder`, {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ initial_dir: initialDir })
                });
                const data = await res.json();
                if (data.status === 'SUCCESS' && data.path) {
                    const fname = cur ? (cur.split('\\').pop() || 'download.bin') : 'download.bin';
                    document.getElementById('info-save-as').value = `${data.path}\\${fname}`;
                }
            } catch (e) {}
        });
        document.getElementById('info-url').addEventListener('change', () => {
            const u = document.getElementById('info-url').value.trim();
            if (u) this.probeUrl(u);
        });

        document.getElementById('btn-resume').addEventListener('click', () => {
            if (this.selectedId) this.resumeTask(this.selectedId);
        });
        document.getElementById('btn-pause').addEventListener('click', () => {
            if (this.selectedId) this.pauseTask(this.selectedId);
        });
        document.getElementById('btn-stop-all').addEventListener('click', () => this.stopAll());
        document.getElementById('btn-delete').addEventListener('click', () => {
            if (this.selectedId) this.promptDeleteTask(this.selectedId);
        });
        document.getElementById('btn-delete-completed').addEventListener('click', () => this.deleteCompleted());

        // Speed Limiter Toggle
        document.getElementById('btn-speed-limiter-toggle').addEventListener('click', () => {
            this.toggleSpeedLimiter();
        });

        // Scheduler Button
        document.getElementById('btn-scheduler').addEventListener('click', async () => {
            document.getElementById('scheduler-dialog').classList.add('open');
            try {
                const res = await fetch(`${this.apiBase}/api/queue_settings`);
                const data = await res.json();
                if (data.start_time) document.getElementById('time-sched-start').value = data.start_time;
                if (data.stop_time) document.getElementById('time-sched-stop').value = data.stop_time;
                if (data.start_scheduled !== undefined) document.getElementById('chk-sched-start-at').checked = data.start_scheduled;
                if (data.stop_scheduled !== undefined) document.getElementById('chk-sched-stop-at').checked = data.stop_scheduled;
                if (data.max_concurrent) document.getElementById('input-queue-concurrency').value = data.max_concurrent;
                if (data.retries) document.getElementById('input-sched-retries').value = data.retries;
                if (data.post_action) {
                    const chkShutdown = document.getElementById('chk-sched-shutdown');
                    if (chkShutdown) {
                        chkShutdown.checked = (data.post_action === 'ShutdownPC' || data.post_action === 'SleepPC' || data.post_action === 'HibernatePC');
                        if (chkShutdown.checked) {
                            const radio = document.querySelector(`input[name="sched_power_action"][value="${data.post_action === 'SleepPC' ? 'sleep' : (data.post_action === 'HibernatePC' ? 'hibernate' : 'shutdown')}"]`);
                            if (radio) radio.checked = true;
                        }
                    }
                }
            } catch (e) {}
        });
        document.getElementById('btn-close-scheduler-dialog').addEventListener('click', () => {
            document.getElementById('scheduler-dialog').classList.remove('open');
        });
        document.getElementById('btn-sched-close').addEventListener('click', () => {
            document.getElementById('scheduler-dialog').classList.remove('open');
        });

        // Options Dialog Open/Close & Tab Switching
        document.getElementById('btn-options').addEventListener('click', async () => {
            document.getElementById('options-dialog').classList.add('open');
            try {
                const res = await fetch(`${this.apiBase}/api/settings`);
                const data = await res.json();
                if (data.file_types) document.getElementById('opt-file-types').value = data.file_types;
                if (data.default_download_directory) document.getElementById('opt-save-dir').value = data.default_download_directory;
                if (data.cache_directory) document.getElementById('opt-cache-dir').value = data.cache_directory;
                if (data.antivirus_executable_path) document.getElementById('opt-av-path').value = data.antivirus_executable_path;
                if (data.show_start_download_dialog !== undefined) {
                    const chkShow = document.getElementById('opt-show-start-dialog');
                    if (chkShow) chkShow.checked = data.show_start_download_dialog;
                }
                const chkSounds = document.getElementById('opt-play-sounds');
                if (chkSounds) chkSounds.checked = localStorage.getItem('idm_play_sounds') !== 'false';

                try {
                    const clipRes = await fetch(`${this.apiBase}/api/clipboard_monitor/status`);
                    const clipData = await clipRes.json();
                    const chkClip = document.getElementById('opt-clipboard-monitor');
                    if (chkClip && clipData.clipboard_monitor_active !== undefined) {
                        chkClip.checked = clipData.clipboard_monitor_active;
                    }
                } catch (e) {}

                this.categoryFolders = data.category_folders || {
                    "Documents": "Documents",
                    "Videos": "Videos",
                    "Music": "Music",
                    "Compressed": "Compressed",
                    "Programs": "Programs"
                };
                this.defaultDownloadDir = data.default_download_directory || "C:\\Downloads";
            } catch (e) {}
        });

        const catSelect = document.getElementById('opt-category-select');
        if (catSelect) {
            catSelect.addEventListener('change', (e) => {
                const cat = e.target.value;
                const baseDir = this.defaultDownloadDir || 'C:\\Downloads';
                if (cat === 'General') {
                    document.getElementById('opt-save-dir').value = baseDir;
                } else if (this.categoryFolders && this.categoryFolders[cat]) {
                    const sub = this.categoryFolders[cat];
                    document.getElementById('opt-save-dir').value = sub.includes(':') ? sub : `${baseDir}\\${sub}`;
                } else {
                    document.getElementById('opt-save-dir').value = `${baseDir}\\${cat}`;
                }
            });
        }

        document.getElementById('btn-close-options').addEventListener('click', () => {
            document.getElementById('options-dialog').classList.remove('open');
        });
        document.getElementById('btn-opt-cancel').addEventListener('click', () => {
            document.getElementById('options-dialog').classList.remove('open');
        });

        const saveOptions = async () => {
            const curCat = document.getElementById('opt-category-select')?.value || 'General';
            const curSave = document.getElementById('opt-save-dir').value.trim();
            if (curCat === 'General') {
                this.defaultDownloadDir = curSave;
            } else if (this.categoryFolders) {
                this.categoryFolders[curCat] = curSave;
            }

            const chkClip = document.getElementById('opt-clipboard-monitor');
            if (chkClip) {
                try {
                    await fetch(`${this.apiBase}/api/clipboard_monitor/enable`, {
                        method: 'POST',
                        headers: { 'Content-Type': 'application/json' },
                        body: JSON.stringify({ enable: chkClip.checked })
                    });
                } catch (e) {}
            }
            const chkSounds = document.getElementById('opt-play-sounds');
            if (chkSounds) {
                localStorage.setItem('idm_play_sounds', chkSounds.checked ? 'true' : 'false');
            }

            try {
                await fetch(`${this.apiBase}/api/settings`, {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({
                        file_types: document.getElementById('opt-file-types').value,
                        default_download_directory: this.defaultDownloadDir || document.getElementById('opt-save-dir').value,
                        cache_directory: document.getElementById('opt-cache-dir').value,
                        antivirus_executable_path: document.getElementById('opt-av-path').value,
                        max_connections: parseInt(document.getElementById('opt-max-conn').value, 10) || 8,
                        show_start_download_dialog: document.getElementById('opt-show-start-dialog')?.checked ?? true,
                        category_folders: this.categoryFolders || {}
                    })
                });
            } catch (e) {}
        };

        document.getElementById('btn-opt-apply').addEventListener('click', saveOptions);
        document.getElementById('btn-opt-ok').addEventListener('click', async () => {
            await saveOptions();
            document.getElementById('options-dialog').classList.remove('open');
        });

        document.querySelectorAll('[data-opt-tab]').forEach(tab => {
            tab.addEventListener('click', () => {
                const target = tab.getAttribute('data-opt-tab');
                document.querySelectorAll('[data-opt-tab]').forEach(t => t.classList.remove('active'));
                tab.classList.add('active');
                document.querySelectorAll('#options-dialog .win-tab-body').forEach(b => b.style.display = 'none');
                const activeBody = document.getElementById(target);
                if (activeBody) activeBody.style.display = 'block';
            });
        });

        // Scheduler Tab Switching & Controls
        const schedScheduleBtn = document.getElementById('tab-sched-schedule-btn');
        const schedFilesBtn = document.getElementById('tab-sched-files-btn');
        const schedScheduleContent = document.getElementById('sched-tab-schedule-content');
        const schedFilesContent = document.getElementById('sched-tab-files-content');

        schedScheduleBtn.addEventListener('click', () => {
            schedScheduleBtn.classList.add('active');
            schedFilesBtn.classList.remove('active');
            schedScheduleContent.style.display = 'flex';
            schedFilesContent.style.display = 'none';
        });

        this.renderQueueFiles = () => {
            const tbody = document.getElementById('sched-queue-files-body');
            if (!tbody) return;
            tbody.innerHTML = '';
            const queuedTasks = this.downloads.filter(t => t.status !== 'Completed');
            if (queuedTasks.length === 0) {
                tbody.innerHTML = '<tr><td colspan="4" style="text-align:center; color:#888; padding: 20px;">Queue is currently empty</td></tr>';
            } else {
                queuedTasks.forEach((t) => {
                    const row = document.createElement('tr');
                    row.style.cursor = 'pointer';
                    if (this.selectedQueueTaskId === t.id) {
                        row.style.backgroundColor = '#CCE8FF';
                        row.style.outline = '1px dotted #3399FF';
                    }
                    row.innerHTML = `
                        <td style="text-align:center;">${this.getFileIcon(t.ext)}</td>
                        <td>${t.filename}</td>
                        <td style="text-align:right;">${t.sizeFormatted}</td>
                        <td style="color: #0000FF;">${t.status}</td>
                    `;
                    row.addEventListener('click', () => {
                        this.selectedQueueTaskId = t.id;
                        this.renderQueueFiles();
                    });
                    tbody.appendChild(row);
                });
            }
        };

        schedFilesBtn.addEventListener('click', () => {
            schedFilesBtn.classList.add('active');
            schedScheduleBtn.classList.remove('active');
            schedScheduleContent.style.display = 'none';
            schedFilesContent.style.display = 'flex';
            this.renderQueueFiles();
        });

        // Queue Reordering & Removal Buttons
        document.getElementById('btn-queue-move-up').addEventListener('click', async () => {
            if (!this.selectedQueueTaskId) return;
            try {
                await fetch(`${this.apiBase}/api/queue_reorder`, {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ id: this.selectedQueueTaskId, direction: 'up' })
                });
                await this.fetchTasks();
                this.renderQueueFiles();
            } catch (e) {}
        });

        document.getElementById('btn-queue-move-down').addEventListener('click', async () => {
            if (!this.selectedQueueTaskId) return;
            try {
                await fetch(`${this.apiBase}/api/queue_reorder`, {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ id: this.selectedQueueTaskId, direction: 'down' })
                });
                await this.fetchTasks();
                this.renderQueueFiles();
            } catch (e) {}
        });

        document.getElementById('btn-queue-remove').addEventListener('click', async () => {
            if (!this.selectedQueueTaskId) return;
            try {
                await fetch(`${this.apiBase}/api/queue_remove`, {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ id: this.selectedQueueTaskId })
                });
                this.selectedQueueTaskId = null;
                await this.fetchTasks();
                this.renderQueueFiles();
            } catch (e) {}
        });

        // Scheduler Action Buttons
        document.getElementById('btn-sched-start-now').addEventListener('click', async () => {
            try {
                await fetch(`${this.apiBase}/api/start_queue`, { method: 'POST' });
                await this.fetchTasks();
            } catch (e) {}
        });

        document.getElementById('btn-sched-stop').addEventListener('click', async () => {
            try {
                await fetch(`${this.apiBase}/api/stop_queue`, { method: 'POST' });
                await this.fetchTasks();
            } catch (e) {}
        });

        document.getElementById('btn-sched-apply').addEventListener('click', async () => {
            let postAction = 'None';
            if (document.getElementById('chk-sched-shutdown').checked) {
                const selectedRadio = document.querySelector('input[name="sched_power_action"]:checked');
                const val = selectedRadio ? selectedRadio.value : 'shutdown';
                if (val === 'sleep') postAction = 'SleepPC';
                else if (val === 'hibernate') postAction = 'HibernatePC';
                else postAction = 'ShutdownPC';
            }

            try {
                await fetch(`${this.apiBase}/api/queue_settings`, {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({
                        max_concurrent: parseInt(document.getElementById('input-queue-concurrency').value, 10) || 3,
                        post_action: postAction,
                        start_time: document.getElementById('time-sched-start').value,
                        stop_time: document.getElementById('time-sched-stop').value,
                        start_scheduled: document.getElementById('chk-sched-start-at')?.checked ?? false,
                        stop_scheduled: document.getElementById('chk-sched-stop-at')?.checked ?? false,
                        retries: parseInt(document.getElementById('input-sched-retries').value, 10) || 10
                    })
                });
            } catch (e) {}
        });

        document.getElementById('btn-sched-apply-files').addEventListener('click', async () => {
            document.getElementById('btn-sched-apply').click();
        });
        document.getElementById('btn-sched-close-files').addEventListener('click', () => {
            document.getElementById('scheduler-dialog').classList.remove('open');
        });

        // Toolbar Queue Buttons
        document.getElementById('btn-start-queue').addEventListener('click', async () => {
            try {
                await fetch(`${this.apiBase}/api/start_queue`, { method: 'POST' });
                await this.fetchTasks();
            } catch (e) {}
        });

        document.getElementById('btn-stop-queue').addEventListener('click', async () => {
            try {
                await fetch(`${this.apiBase}/api/stop_queue`, { method: 'POST' });
                await this.fetchTasks();
            } catch (e) {}
        });

        // Download Status Dialog Controls
        document.getElementById('btn-popout-status')?.addEventListener('click', () => {
            if (this.activeInspectId) {
                const task = this.downloads.find(t => t.id === this.activeInspectId || this.isSameTask(t.id, this.activeInspectId));
                if (task) {
                    this.openIndependentStatusWindow(task);
                }
            }
        });
        document.getElementById('btn-close-status-dialog').addEventListener('click', () => this.closeStatusDialog());
        document.getElementById('btn-toggle-details').addEventListener('click', () => this.toggleDetails());
        document.getElementById('btn-dlg-pause').addEventListener('click', () => {
            if (this.activeInspectId) {
                const task = this.downloads.find(t => t.id === this.activeInspectId);
                if (task) {
                    if (task.status === 'Paused' || task.status === 'Cancelled' || task.status === 'NotStarted') {
                        this.resumeTask(task.id);
                    } else {
                        this.pauseTask(task.id);
                    }
                }
            }
        });
        document.getElementById('btn-dlg-cancel').addEventListener('click', () => {
            if (this.activeInspectId) {
                const task = this.downloads.find(t => t.id === this.activeInspectId || this.isSameTask(t.id, this.activeInspectId));
                if (task && task.status === 'Completed') {
                    this.closeStatusDialog();
                    return;
                }
                this.cancelTask(this.activeInspectId);
                this.closeStatusDialog();
            }
        });

        // Delete Confirmation Dialog Controls
        document.getElementById('btn-confirm-delete')?.addEventListener('click', () => {
            if (this.pendingDeleteId) {
                const deleteDisk = document.getElementById('chk-delete-disk-file')?.checked || false;
                this.deleteTask(this.pendingDeleteId, deleteDisk);
            }
            this.closeDeleteDialog();
        });
        document.getElementById('btn-cancel-delete')?.addEventListener('click', () => this.closeDeleteDialog());
        document.getElementById('btn-close-delete-dialog')?.addEventListener('click', () => this.closeDeleteDialog());

        // Download Complete Dialog Controls
        const btnCloseComplete = document.getElementById('btn-close-complete-dialog');
        if (btnCloseComplete) {
            btnCloseComplete.addEventListener('click', () => this.closeCompleteDialog());
        }
        const btnCompleteClose = document.getElementById('btn-complete-close');
        if (btnCompleteClose) {
            btnCompleteClose.addEventListener('click', () => this.closeCompleteDialog());
        }

        // Speed Limiter Tab Apply Button
        const btnApplyLimiter = document.getElementById('btn-apply-limiter');
        if (btnApplyLimiter) {
            btnApplyLimiter.addEventListener('click', async () => {
                const useLimiter = document.getElementById('chk-use-limiter').checked;
                const limitVal = parseInt(document.getElementById('input-limiter-val').value, 10) || 500;
                const speedBps = useLimiter ? limitVal * 1024 : 0;
                if (this.activeInspectId) {
                    try {
                        await fetch(`${this.apiBase}/api/speed_limiter`, {
                            method: 'POST',
                            headers: { 'Content-Type': 'application/json' },
                            body: JSON.stringify({
                                id: this.activeInspectId,
                                speed_limit_bps: speedBps
                            })
                        });
                        this.showMessageBox('Speed limiter set to ' + (useLimiter ? limitVal + ' KB/s' : 'Uncapped'), 'Speed Limiter', 'ℹ️');
                    } catch (e) {}
                }
            });
        }

        // Options Dialog Browse Buttons (Native Windows Folder/File Dialogs)
        const btnBrowseSave = document.getElementById('btn-opt-browse-save');
        if (btnBrowseSave) {
            btnBrowseSave.addEventListener('click', async () => {
                const cur = document.getElementById('opt-save-dir').value || 'C:\\Downloads';
                try {
                    const res = await fetch(`${this.apiBase}/api/browse_folder`, {
                        method: 'POST',
                        headers: { 'Content-Type': 'application/json' },
                        body: JSON.stringify({ initial_dir: cur })
                    });
                    const data = await res.json();
                    if (data.status === 'SUCCESS' && data.path) {
                        document.getElementById('opt-save-dir').value = data.path;
                    }
                } catch (e) {}
            });
        }
        const btnBrowseCache = document.getElementById('btn-opt-browse-cache');
        if (btnBrowseCache) {
            btnBrowseCache.addEventListener('click', async () => {
                const cur = document.getElementById('opt-cache-dir').value || 'C:\\Downloads\\Cache';
                try {
                    const res = await fetch(`${this.apiBase}/api/browse_folder`, {
                        method: 'POST',
                        headers: { 'Content-Type': 'application/json' },
                        body: JSON.stringify({ initial_dir: cur })
                    });
                    const data = await res.json();
                    if (data.status === 'SUCCESS' && data.path) {
                        document.getElementById('opt-cache-dir').value = data.path;
                    }
                } catch (e) {}
            });
        }
        const btnBrowseAv = document.getElementById('btn-opt-browse-av');
        if (btnBrowseAv) {
            btnBrowseAv.addEventListener('click', async () => {
                try {
                    const res = await fetch(`${this.apiBase}/api/browse_file`, { method: 'POST' });
                    const data = await res.json();
                    if (data.status === 'SUCCESS' && data.path) {
                        document.getElementById('opt-av-path').value = data.path;
                    }
                } catch (e) {}
            });
        }

        // Scheduler Queue Add/Delete Buttons
        const btnNewQueue = document.getElementById('btn-new-queue');
        if (btnNewQueue) {
            btnNewQueue.addEventListener('click', () => {
                this.showMessageBox('New queue setup is automatically managed by IDM Scheduler.', 'Scheduler Queues', 'ℹ️');
            });
        }
        const btnDelQueue = document.getElementById('btn-del-queue');
        if (btnDelQueue) {
            btnDelQueue.addEventListener('click', () => {
                this.showMessageBox('Cannot delete the default Main Queue.', 'Scheduler Queues', '⚠️');
            });
        }

        // Download Status Dialog Tab Switching
        document.querySelectorAll('.win-tab-item').forEach(tab => {
            tab.addEventListener('click', () => {
                const tabTarget = tab.getAttribute('data-tab');
                if (!tabTarget) return;

                document.querySelectorAll('.win-tab-item').forEach(t => t.classList.remove('active'));
                tab.classList.add('active');

                document.querySelectorAll('.win-tab-body').forEach(body => {
                    body.style.display = 'none';
                });
                const activeBody = document.getElementById(tabTarget);
                if (activeBody) activeBody.style.display = 'block';
            });
        });

        // Categories Sidebar Tree Selection
        document.querySelectorAll('.tree-node').forEach(node => {
            node.addEventListener('click', () => {
                document.querySelectorAll('.tree-node').forEach(n => n.classList.remove('active'));
                node.classList.add('active');

                const filter = node.getAttribute('data-filter');
                const cat = node.getAttribute('data-category');

                if (cat) {
                    this.activeFilter = 'category';
                    this.activeCategory = cat;
                } else if (filter) {
                    this.activeFilter = filter;
                    this.activeCategory = null;
                }
                this.renderTable();
            });
        });

        // Hide/Show Categories Button
        const sidebar = document.getElementById('sidebar-container');
        document.getElementById('btn-close-sidebar').addEventListener('click', () => {
            sidebar.style.display = sidebar.style.display === 'none' ? 'flex' : 'none';
        });
        document.getElementById('menu-toggle-sidebar').addEventListener('click', () => {
            sidebar.style.display = sidebar.style.display === 'none' ? 'flex' : 'none';
        });

        // Menu Bar Actions
        const menuAddUrl = document.getElementById('menu-add-url');
        if (menuAddUrl) {
            menuAddUrl.addEventListener('click', () => {
                document.getElementById('add-url-dialog').classList.add('open');
            });
        }
        const menuBatchAdd = document.getElementById('menu-batch-add');
        if (menuBatchAdd) {
            menuBatchAdd.addEventListener('click', () => {
                document.getElementById('add-url-dialog').classList.add('open');
                document.getElementById('input-new-url').focus();
            });
        }
        const menuExport = document.getElementById('menu-export');
        if (menuExport) {
            menuExport.addEventListener('click', () => {
                const dataStr = "data:text/json;charset=utf-8," + encodeURIComponent(JSON.stringify(this.downloads, null, 2));
                const dlAnchor = document.createElement('a');
                dlAnchor.setAttribute("href", dataStr);
                dlAnchor.setAttribute("download", "idm_downloads.json");
                dlAnchor.click();
            });
        }
        const menuImport = document.getElementById('menu-import');
        if (menuImport) {
            menuImport.addEventListener('click', () => {
                document.getElementById('add-url-dialog').classList.add('open');
                document.getElementById('input-new-url').focus();
            });
        }
        const menuOpenCache = document.getElementById('menu-open-cache');
        if (menuOpenCache) {
            menuOpenCache.addEventListener('click', () => {
                const cacheDir = document.getElementById('opt-cache-dir')?.value || 'C:\\Downloads\\Cache';
                this.openFolder(cacheDir);
            });
        }
        const menuClearCache = document.getElementById('menu-clear-cache');
        if (menuClearCache) {
            menuClearCache.addEventListener('click', () => {
                this.showMessageBox('Centralized cache cleared successfully.', 'Clear Cache', '✔️');
            });
        }
        const menuSpeedLimiter = document.getElementById('menu-speed-limiter');
        if (menuSpeedLimiter) {
            menuSpeedLimiter.addEventListener('click', () => {
                this.toggleSpeedLimiter();
            });
        }
        const menuHelpBtn = document.getElementById('menu-help-btn');
        if (menuHelpBtn) {
            menuHelpBtn.addEventListener('click', () => {
                this.showAboutDialog();
            });
        }
        const menuRegBtn = document.getElementById('menu-reg-btn');
        if (menuRegBtn) {
            menuRegBtn.addEventListener('click', () => {
                this.showRegistrationDialog();
            });
        }

        // About & Registration dialog close listeners
        document.getElementById('btn-close-about')?.addEventListener('click', () => this.closeAboutDialog());
        document.getElementById('btn-about-ok')?.addEventListener('click', () => this.closeAboutDialog());
        document.getElementById('btn-close-registration')?.addEventListener('click', () => this.closeRegistrationDialog());
        document.getElementById('btn-registration-close')?.addEventListener('click', () => this.closeRegistrationDialog());

        // Dark Mode Toggle listeners
        document.getElementById('btn-theme-toggle')?.addEventListener('click', () => this.setDarkMode(!this.isDarkMode));
        document.getElementById('menu-view-darkmode')?.addEventListener('click', () => this.setDarkMode(!this.isDarkMode));

        document.getElementById('menu-resume').addEventListener('click', () => {
            if (this.selectedId) this.resumeTask(this.selectedId);
        });
        document.getElementById('menu-pause').addEventListener('click', () => {
            if (this.selectedId) this.pauseTask(this.selectedId);
        });
        document.getElementById('menu-stop-all').addEventListener('click', () => this.stopAll());
        // Categories Collapsible Branch Toggle
        const toggleCatBtn = document.getElementById('toggle-categories-branch');
        const subbranch = document.getElementById('categories-subbranch');
        if (toggleCatBtn && subbranch) {
            toggleCatBtn.addEventListener('click', (e) => {
                e.stopPropagation();
                const isCollapsed = subbranch.classList.toggle('collapsed');
                toggleCatBtn.textContent = isCollapsed ? '+' : '-';
            });
        }

        document.getElementById('menu-delete').addEventListener('click', () => {
            if (this.selectedId) this.promptDeleteTask(this.selectedId);
        });
        document.getElementById('menu-delete-completed').addEventListener('click', () => this.deleteCompleted());
        document.getElementById('menu-refresh').addEventListener('click', () => this.fetchTasks());
        document.getElementById('menu-open-downloads').addEventListener('click', () => {
            const saveDir = document.getElementById('opt-save-dir')?.value || 'C:\\Downloads';
            this.openFolder(saveDir);
        });
        document.getElementById('menu-exit').addEventListener('click', async () => {
            try {
                await fetch(`${this.apiBase}/api/exit`, { method: 'POST' });
            } catch (e) {}
        });

        // Global Keyboard Shortcuts (Escape to close modals, Delete key to prompt delete)
        window.addEventListener('keydown', (e) => {
            if (e.key === 'Escape') {
                if (document.getElementById('delete-confirm-dialog')?.classList.contains('open')) {
                    this.closeDeleteDialog();
                } else if (document.getElementById('custom-win-messagebox')?.classList.contains('open')) {
                    document.getElementById('custom-win-messagebox').classList.remove('open');
                } else if (document.getElementById('download-complete-dialog')?.classList.contains('open')) {
                    this.closeCompleteDialog();
                } else if (document.getElementById('download-status-dialog')?.classList.contains('open')) {
                    this.closeStatusDialog();
                }
            } else if (e.key === 'Delete') {
                const tag = document.activeElement ? document.activeElement.tagName.toLowerCase() : '';
                if (tag !== 'input' && tag !== 'textarea' && tag !== 'select') {
                    if (this.selectedId) {
                        this.promptDeleteTask(this.selectedId);
                    }
                }
            }
        });
    },

    initDialogDragging() {
        document.querySelectorAll('.win-dialog-overlay').forEach(overlay => {
            const box = overlay.querySelector('.win-dialog-box');
            const titleBar = overlay.querySelector('.dialog-title-bar');
            if (!box || !titleBar) return;

            let isDragging = false;
            let startX = 0, startY = 0;
            let initialLeft = 0, initialTop = 0;

            titleBar.addEventListener('mousedown', (e) => {
                if (e.target.closest('button, .dialog-close-btn, input, a')) return;

                isDragging = true;
                startX = e.clientX;
                startY = e.clientY;

                const rect = box.getBoundingClientRect();
                box.style.position = 'absolute';
                box.style.margin = '0';
                box.style.left = `${rect.left}px`;
                box.style.top = `${rect.top}px`;
                initialLeft = rect.left;
                initialTop = rect.top;

                const onMouseMove = (ev) => {
                    if (!isDragging) return;
                    const dx = ev.clientX - startX;
                    const dy = ev.clientY - startY;
                    let newLeft = initialLeft + dx;
                    let newTop = initialTop + dy;

                    const maxLeft = Math.max(10, window.innerWidth - 60);
                    const maxTop = Math.max(10, window.innerHeight - 30);
                    newLeft = Math.max(0, Math.min(newLeft, maxLeft));
                    newTop = Math.max(0, Math.min(newTop, maxTop));

                    box.style.left = `${newLeft}px`;
                    box.style.top = `${newTop}px`;
                };

                const onMouseUp = () => {
                    isDragging = false;
                    window.removeEventListener('mousemove', onMouseMove);
                    window.removeEventListener('mouseup', onMouseUp);
                };

                window.addEventListener('mousemove', onMouseMove);
                window.addEventListener('mouseup', onMouseUp);
            });
        });
    },

    initDropTarget() {
        const dropTarget = document.getElementById('floating-drop-target');
        const menuDropTarget = document.getElementById('menu-view-drop-target');
        const checkIcon = document.getElementById('droptarget-menu-check');
        if (!dropTarget) return;

        const setDropTargetVisible = (visible) => {
            if (visible) {
                dropTarget.classList.remove('hidden');
                if (checkIcon) checkIcon.textContent = '✓';
                localStorage.setItem('idm_show_drop_target', 'true');
            } else {
                dropTarget.classList.add('hidden');
                if (checkIcon) checkIcon.textContent = '';
                localStorage.setItem('idm_show_drop_target', 'false');
            }
        };

        if (menuDropTarget) {
            menuDropTarget.addEventListener('click', () => {
                const isHidden = dropTarget.classList.contains('hidden');
                setDropTargetVisible(isHidden);
            });
        }

        dropTarget.addEventListener('dblclick', () => {
            setDropTargetVisible(false);
        });

        if (localStorage.getItem('idm_show_drop_target') === 'false') {
            setDropTargetVisible(false);
        }

        // Window drag repositioning
        let isDragging = false;
        let startX = 0, startY = 0;
        let initialLeft = 0, initialTop = 0;

        dropTarget.addEventListener('mousedown', (e) => {
            if (e.button !== 0) return;
            isDragging = true;
            startX = e.clientX;
            startY = e.clientY;
            const rect = dropTarget.getBoundingClientRect();
            dropTarget.style.right = 'auto';
            dropTarget.style.bottom = 'auto';
            dropTarget.style.left = `${rect.left}px`;
            dropTarget.style.top = `${rect.top}px`;
            initialLeft = rect.left;
            initialTop = rect.top;

            const onMouseMove = (ev) => {
                if (!isDragging) return;
                const dx = ev.clientX - startX;
                const dy = ev.clientY - startY;
                let newLeft = Math.max(10, Math.min(window.innerWidth - 100, initialLeft + dx));
                let newTop = Math.max(10, Math.min(window.innerHeight - 90, initialTop + dy));
                dropTarget.style.left = `${newLeft}px`;
                dropTarget.style.top = `${newTop}px`;
            };

            const onMouseUp = () => {
                isDragging = false;
                window.removeEventListener('mousemove', onMouseMove);
                window.removeEventListener('mouseup', onMouseUp);
            };

            window.addEventListener('mousemove', onMouseMove);
            window.addEventListener('mouseup', onMouseUp);
        });

        // Drag-and-drop link ingestion
        dropTarget.addEventListener('dragover', (e) => {
            e.preventDefault();
            e.stopPropagation();
            dropTarget.classList.add('drag-over');
            e.dataTransfer.dropEffect = 'copy';
        });

        dropTarget.addEventListener('dragleave', (e) => {
            e.preventDefault();
            e.stopPropagation();
            dropTarget.classList.remove('drag-over');
        });

        dropTarget.addEventListener('drop', (e) => {
            e.preventDefault();
            e.stopPropagation();
            dropTarget.classList.remove('drag-over');

            let url = '';
            const uriList = e.dataTransfer.getData('text/uri-list');
            if (uriList) {
                const lines = uriList.split('\n').map(l => l.trim()).filter(l => l && !l.startsWith('#'));
                if (lines.length > 0) url = lines[0];
            }
            if (!url) {
                const html = e.dataTransfer.getData('text/html');
                if (html) {
                    const match = html.match(/href=["'](https?:\/\/[^"']+)["']/i);
                    if (match) url = match[1];
                }
            }
            if (!url) {
                const text = e.dataTransfer.getData('text/plain');
                if (text && (text.startsWith('http://') || text.startsWith('https://'))) {
                    url = text.trim();
                }
            }

            if (url) {
                this.openDownloadFileInfoDialog({
                    url: url,
                    filename: url.substring(url.lastIndexOf('/') + 1)
                });
            }
        });
    }
};

document.addEventListener('DOMContentLoaded', () => {
    IDMApp.init();
});
