/* ==========================================================================
   IDM Core Frontend Application Controller (Full Specification)
   ========================================================================== */

const App = {
    downloads: [],
    selectedId: null,
    activeFilter: 'all',
    activeCategory: null,
    interceptionEnabled: true,

    init() {
        this.bindEvents();
        this.renderTable();
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

    submitNewDownload() {
        const urlInput = document.getElementById('input-download-url').value.trim();
        const category = document.getElementById('input-category').value;
        const threads = parseInt(document.getElementById('input-threads').value, 10);

        if (!urlInput) {
            alert('Please enter a valid HTTP/HTTPS download URL.');
            return;
        }

        const filename = urlInput.substring(urlInput.lastIndexOf('/') + 1) || 'download_file.bin';

        const newDownload = {
            id: Date.now().toString(),
            filename: filename,
            url: urlInput,
            category: category,
            sizeFormatted: 'Fetching...',
            totalBytes: 0,
            downloadedBytes: 0,
            status: 'Downloading',
            progress: 0,
            speed: '0.0 KB/s',
            eta: '--',
            threads: threads,
            chunks: []
        };

        this.downloads.unshift(newDownload);
        this.closeModal('add-url-modal');
        this.renderTable();
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

    resumeSelected() {
        if (!this.selectedId) return;
        const task = this.downloads.find(d => d.id === this.selectedId);
        if (task && (task.status === 'Paused' || task.status === 'Failed')) {
            task.status = 'Downloading';
            this.renderTable();
        }
    },

    pauseSelected() {
        if (!this.selectedId) return;
        const task = this.downloads.find(d => d.id === this.selectedId);
        if (task && task.status === 'Downloading') {
            task.status = 'Paused';
            task.speed = '--';
            task.eta = '--';
            this.renderTable();
        }
    },

    stopAll() {
        this.downloads.forEach(task => {
            if (task.status === 'Downloading') {
                task.status = 'Paused';
                task.speed = '--';
                task.eta = '--';
            }
        });
        this.renderTable();
    },

    deleteSelected() {
        if (!this.selectedId) return;
        this.downloads = this.downloads.filter(d => d.id !== this.selectedId);
        this.selectedId = null;
        this.renderTable();
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
