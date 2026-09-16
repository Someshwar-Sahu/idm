const ChunkVisualizer = {
    modal: null,
    filenameEl: null,
    statusEl: null,
    speedEl: null,
    etaEl: null,
    overallFillEl: null,
    containerEl: null,
    activeTaskFilename: null,

    init() {
        this.modal = document.getElementById('visualizer-modal');
        this.filenameEl = document.getElementById('viz-filename');
        this.statusEl = document.getElementById('viz-status');
        this.speedEl = document.getElementById('viz-speed');
        this.etaEl = document.getElementById('viz-eta');
        this.overallFillEl = document.getElementById('viz-overall-fill');
        this.containerEl = document.getElementById('chunk-container');

        const closeBtn = document.getElementById('btn-close-viz-modal');
        if(closeBtn) {
            closeBtn.addEventListener('click', () => this.close());
        }

        if(this.modal){
            this.modal.addEventListener('click', (e) => {
                if(e.target == this.modal) this.close();
            });
        }
    },

    open(task){
        if(!task || !this.modal) return;
        this.activeTaskFilename = task.filename;
        this.filenameEl.textContent = `Progress Visualizer: ${task.filename}`;
        this.modal.classList.add('open');
        this.update(task);
    },

    close() {
        if(this.modal){
            this.modal.classList.remove('open');
        }
        this.activeTaskFilename = null;
    },

    update(task){
        if(!task || this.activeTaskFilename !== task.filename) return;

        this.statusEl.textContent = task.status;
        this.speedEl.textContent = task.speed || '--';
        this.etaEl.textContent = task.eta || '--';
        this.overallFillEl.style.width = `${task.progress || 0}%`;

        this.renderChunks(task.chunks || []);
    },

    renderChunks(chunks){
        if(!this.containerEl) return;
        if(chunks.length === 0){
            this.containerEl.innerHTML = `
                <div style="font-size: 12px; color: var(--text-muted); text-align: center; padding: 12px;">
                    Single-stream download active (1 worker thread)
                </div>`;
            return ;
        }
        this.containerEl.innerHTML = '';

        chunks.forEach((chunk, index) => {
            const chunkRow = document.createElement('div');
            chunkRow.className = 'chunk-row';

            const pct = chunk.total > 0 ? ((chunk.downloaded / chunk.total) * 100).toFixed(1) : 0;

            chunkRow.innerHTML = `
                <span class="chunk-label">Thread ${index + 1}</span>
                <div class="chunk-bar-track">
                    <div class="chunk-bar-fill" style="width: ${pct}%;"></div>
                </div>
                <span style="width: 45px; text-align: right; color: var(--text-muted); font-size: 11px;">${pct}%</span>
            `;

            this.containerEl.appendChild(chunkRow);
        });
    }
};

document.addEventListener('DOMContentLoaded', () => {
    ChunkVisualizer.init();
})