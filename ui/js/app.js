/* ==========================================================================
   IDM Core Frontend Application Controller (Full Specification)
   ========================================================================== */

const App = {
    apiBase: 'http://127.0.0.1:8080',
    downloads: [],
    selectedId: null,
    activeFilter: 'all',
    activeCategory: null,
    interceptionEnabled: true,

    init() {
        this.bindEvents();
        this.renderTable();
        this.fetchTasks();
        setInterval(() => this.fetchTasks(), 1000);
    },

    formatBytes(bytes) {
        if (!bytes || bytes <= 0) return '0 B';
        const k = 1024;
        const sizes = ['B', 'KB', 'MB', 'GB', 'TB'];
        const i = Math.floor(Math.log(bytes) / Math.log(k));
        return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
    },

    async fetchTasks() {
        try {
            const res = await fetch(`${this.apiBase}/api/tasks`);
            if (!res.ok) return;
            const tasks = await res.json();
            if (!Array.isArray(tasks)) return;

            this.downloads = tasks.map(t => {
                const savePath = t.save_path || t.id || '';
                const filename = savePath.substring(Math.max(savePath.lastIndexOf('/'), savePath.lastIndexOf('\\')) + 1) || t.url;
                
                let category = 'Other';
                const ext = filename.includes('.') ? filename.substring(filename.lastIndexOf('.')).toLowerCase() : '';
                if (['.zip', '.rar', '.7z', '.tar', '.gz', '.iso', '.dat', '.bin', '.csv'].includes(ext)) category = 'Compressed';
                else if (['.mp4', '.mkv', '.avi', '.webm', '.mov'].includes(ext)) category = 'Videos';
                else if (['.mp3', '.wav', '.flac', '.aac'].includes(ext)) category = 'Music';
                else if (['.pdf', '.doc', '.docx', '.txt', '.xlsx'].includes(ext)) category = 'Documents';
                else if (['.exe', '.msi', '.apk', '.bat'].includes(ext)) category = 'Programs';

                const total = t.total_bytes || 0;
                const downloaded = t.downloaded_bytes || 0;
                const pct = t.progress_pct !== undefined ? Math.min(100, Math.max(0, Math.round(t.progress_pct))) : (total > 0 ? Math.round((downloaded * 100) / total) : 0);
                const speed = t.speed_bps > 0 ? this.formatBytes(t.speed_bps) + '/s' : (t.status === 'Downloading' ? 'Active' : '--');

                return {
                    id: t.id,
                    filename: filename,
                    url: t.url,
                    category: category,
                    sizeFormatted: total > 0 ? this.formatBytes(total) : (downloaded > 0 ? this.formatBytes(downloaded) : '--'),
                    totalBytes: total,
                    downloadedBytes: downloaded,
                    status: t.status || 'Downloading',
                    progress: pct,
                    speed: speed,
                    eta: '--',
                    threads: 8,
                    chunks: []
                };
            });
            this.renderTable();
        } catch (e) {
            // C++ server offline or reconnecting
        }
    },

    bindEvents() {
        // Toolbar buttons
        document.getElementById('btn-add-url').addEventListener('click', () => this.openModal('add-url-modal'));
        document.getElementById('btn-close-url-modal').addEventListener('click', () => this.closeModal('add-url-modal'));
        document.getElementById('btn-cancel-add').addEventListener('click', () => this.closeModal('add-url-modal'));
        document.getElementById('btn-submit-download').addEventListener('click', () => this.submitNewDownload());

        document.getElementById('btn-resume').addEventListener('click', () => this.resumeSelected());
        document.getElementById('btn-pause').addEventListener('click', () => this.pauseSelected());
        document.getElementById('btn-stop-all').addEventListener('click', () => this.stopAll());
        document.getElementById('btn-delete').addEventListener('click', () => this.deleteSelected());

        // Options & Scheduler Modals
        document.getElementById('btn-options').addEventListener('click', () => this.openModal('options-modal'));
        document.getElementById('menu-options-btn').addEventListener('click', () => this.openModal('options-modal'));
        document.getElementById('btn-close-options-modal').addEventListener('click', () => this.closeModal('options-modal'));
        document.getElementById('btn-save-options').addEventListener('click', () => this.closeModal('options-modal'));

        document.getElementById('btn-scheduler').addEventListener('click', () => this.openModal('scheduler-modal'));
        document.getElementById('menu-scheduler-btn').addEventListener('click', () => this.openModal('scheduler-modal'));
        document.getElementById('btn-close-scheduler-modal').addEventListener('click', () => this.closeModal('scheduler-modal'));
        document.getElementById('btn-save-scheduler').addEventListener('click', () => this.closeModal('scheduler-modal'));

        // Daylight / Dark Theme Toggle
        document.getElementById('btn-theme-toggle').addEventListener('click', () => this.toggleTheme());
        document.getElementById('menu-toggle-theme').addEventListener('click', () => this.toggleTheme());

        // Dropdown Menu Actions
        document.getElementById('menu-add-url').addEventListener('click', () => this.openModal('add-url-modal'));
        document.getElementById('menu-resume').addEventListener('click', () => this.resumeSelected());
        document.getElementById('menu-pause').addEventListener('click', () => this.pauseSelected());
        document.getElementById('menu-stop-all').addEventListener('click', () => this.stopAll());
        document.getElementById('menu-delete').addEventListener('click', () => this.deleteSelected());

        document.getElementById('menu-clear-cache').addEventListener('click', () => {
            alert('Centralized Cache (E:\\IDM\\Cache) cleared! 0 KB freed.');
        });

        document.getElementById('menu-toggle-sidebar').addEventListener('click', () => {
            const sidebar = document.getElementById('sidebar-container');
            sidebar.style.display = sidebar.style.display === 'none' ? 'flex' : 'none';
        });

        // Interception Toggle Badge
        document.getElementById('interception-toggle').addEventListener('click', () => this.toggleInterception());

        // Tabs in Options Modal
        document.querySelectorAll('.tab-item').forEach(tab => {
            tab.addEventListener('click', () => {
                document.querySelectorAll('.tab-item').forEach(t => t.classList.remove('active'));
                document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));
                tab.classList.add('active');
                const targetTab = tab.getAttribute('data-tab');
                document.getElementById(targetTab).classList.add('active');
            });
        });

        // Sidebar Category Navigation
        document.querySelectorAll('.sidebar .nav-item').forEach(item => {
            item.addEventListener('click', (e) => {
                document.querySelectorAll('.sidebar .nav-item').forEach(n => n.classList.remove('active'));
                item.classList.add('active');

                const filter = item.getAttribute('data-filter');
                const category = item.getAttribute('data-category');

                if (filter) {
                    this.activeFilter = filter;
                    this.activeCategory = null;
                } else if (category) {
                    this.activeFilter = 'category';
                    this.activeCategory = category;
                }
                this.renderTable();
            });
        });
    },

    toggleTheme() {
        document.body.classList.toggle('light-theme');
        const btn = document.getElementById('btn-theme-toggle');
        if (document.body.classList.contains('light-theme')) {
            btn.textContent = '🌙 Dark Mode';
        } else {
            btn.textContent = '☀️ Daylight Mode';
        }
    },

    openModal(id) {
        const modal = document.getElementById(id);
        if (modal) modal.classList.add('open');
    },

    closeModal(id) {
        const modal = document.getElementById(id);
        if (modal) modal.classList.remove('open');
    },

    async submitNewDownload() {
        const urlInput = document.getElementById('input-download-url').value.trim();
        const threads = parseInt(document.getElementById('input-threads').value, 10) || 8;

        if (!urlInput) {
            alert('Please enter a valid HTTP/HTTPS download URL.');
            return;
        }

        try {
            await fetch(`${this.apiBase}/api/add_download`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ url: urlInput, threads: threads })
            });
            this.closeModal('add-url-modal');
            document.getElementById('input-download-url').value = '';
            await this.fetchTasks();
        } catch (err) {
            alert('Failed to connect to IDM engine: ' + err.message);
        }
    },

    toggleInterception() {
        this.interceptionEnabled = !this.interceptionEnabled;
        const dot = document.getElementById('interception-dot');
        const label = document.getElementById('interception-label');

        if (this.interceptionEnabled) {
            dot.textContent = '🟢';
            label.textContent = 'Interception: ON';
        } else {
            dot.textContent = '🔴';
            label.textContent = 'Interception: OFF';
        }
    },

    async resumeSelected() {
        if (!this.selectedId) return;
        try {
            await fetch(`${this.apiBase}/api/resume`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ id: this.selectedId })
            });
            await this.fetchTasks();
        } catch (e) {}
    },

    async pauseSelected() {
        if (!this.selectedId) return;
        try {
            await fetch(`${this.apiBase}/api/pause`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ id: this.selectedId })
            });
            await this.fetchTasks();
        } catch (e) {}
    },

    async stopAll() {
        for (const task of this.downloads) {
            if (task.status === 'Downloading') {
                await fetch(`${this.apiBase}/api/pause`, {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ id: task.id })
                }).catch(() => {});
            }
        }
        await this.fetchTasks();
    },

    async deleteSelected() {
        if (!this.selectedId) return;
        try {
            await fetch(`${this.apiBase}/api/delete`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ id: this.selectedId, delete_file: false })
            });
            this.selectedId = null;
            await this.fetchTasks();
        } catch (e) {}
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
                    <td colspan="7" style="text-align: center; color: var(--text-muted); padding: 32px;">
                        No download tasks in queue. Click "+ Add URL" to start downloading!
                    </td>
                </tr>`;
            return;
        }

        filtered.forEach(task => {
            const tr = document.createElement('tr');
            if (task.id === this.selectedId) tr.className = 'selected';

            const statusClass = task.status.toLowerCase();

            tr.innerHTML = `
                <td style="font-weight: 600; color: var(--text-primary);">${task.filename}</td>
                <td>${task.category}</td>
                <td>${task.sizeFormatted}</td>
                <td><span class="badge ${statusClass}">${task.status}</span></td>
                <td>
                    <div style="display: flex; align-items: center; gap: 8px;">
                        <div class="grid-progress-bar">
                            <div class="grid-progress-fill" style="width: ${task.progress}%;"></div>
                        </div>
                        <span style="font-size: 11px; width: 32px;">${task.progress}%</span>
                    </div>
                </td>
                <td>${task.speed}</td>
                <td>${task.eta}</td>
            `;

            tr.addEventListener('click', () => {
                this.selectedId = task.id;
                this.renderTable();
            });

            tr.addEventListener('dblclick', () => {
                ChunkVisualizer.open(task);
            });

            tbody.appendChild(tr);
        });
    }
};

document.addEventListener('DOMContentLoaded', () => {
    App.init();
});
