// Tasks Monitor Module

async function refreshTasks() {
    try {
        const data = await api('tasks');

        document.getElementById('heap-free').textContent = formatBytes(data.heap_free);
        document.getElementById('heap-min').textContent = formatBytes(data.heap_min);
        document.getElementById('uptime').textContent = formatUptime(data.uptime_s * 1000);
        document.getElementById('task-count').textContent = data.task_count;

        const tasks = data.tasks.sort((a, b) => b.cpu_percent - a.cpu_percent);

        const tbody = document.getElementById('tasks-tbody');
        tbody.innerHTML = tasks.map(task => `
            <tr>
                <td><strong>${task.name}</strong></td>
                <td><span class="state-${task.state}">${task.state}</span></td>
                <td>${task.priority}</td>
                <td>${task.cpu_percent}%</td>
                <td>${formatBytes(task.stack_hwm * 4)}</td>
            </tr>
        `).join('');

        const idleTasks = tasks.filter(t => t.name === 'IDLE0' || t.name === 'IDLE1');
        const otherTasks = tasks.filter(t => t.name !== 'IDLE0' && t.name !== 'IDLE1');

        const coreUsageHtml = idleTasks.map(task => {
            const coreNum = task.name === 'IDLE0' ? '0' : '1';
            const usagePercent = 100 - task.cpu_percent;
            return `
            <div class="cpu-bar">
                <span class="cpu-bar-label">Core ${coreNum}</span>
                <div class="cpu-bar-track">
                    <div class="cpu-bar-fill" style="width: ${Math.min(usagePercent, 100)}%"></div>
                </div>
                <span class="cpu-bar-value">${usagePercent}%</span>
            </div>`;
        }).join('');

        const otherTasksHtml = otherTasks.slice(0, 4).map(task => `
            <div class="cpu-bar">
                <span class="cpu-bar-label">${task.name}</span>
                <div class="cpu-bar-track">
                    <div class="cpu-bar-fill" style="width: ${Math.min(task.cpu_percent, 100)}%"></div>
                </div>
                <span class="cpu-bar-value">${task.cpu_percent}%</span>
            </div>
        `).join('');

        document.getElementById('cpu-bars').innerHTML = coreUsageHtml + otherTasksHtml;

    } catch (error) {
        console.error('Failed to fetch tasks:', error);
    }
}

function initTasksTab() {
    refreshTasks();
}

registerModule('tasks', initTasksTab, { pollFn: refreshTasks, pollInterval: 2000 });
