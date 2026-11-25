(() => {
    'use strict';
    const statuses = {
        candidate: '自动预览',
        startup_screen: '启动提示未跳过',
        shared_screen: '疑似重复启动画面',
        runtime_error: '运行失败',
        capture_failed: '截图失败',
        blank_screen: '空白画面',
        invalid_package: '包结构异常',
        pending: '等待截图',
        not_captured: '尚未截图',
        review_required: '截图待复核',
    };
    function publicURL(value) {
        if (typeof value !== 'string') throw new Error('资源链接不是字符串');
        const url = new URL(value);
        if (!['http:', 'https:'].includes(url.protocol)) throw new Error('资源链接必须使用 HTTP(S)');
        return value;
    }
    function validateCatalog(data) {
        if (!data || data.schema_version !== 1 || !Array.isArray(data.packages) ||
            data.summary?.total !== data.packages.length) throw new Error('资源目录格式或数量不正确');
        publicURL(data.source_url);
        for (const entry of data.packages) {
            if (!entry || !/^[a-f0-9]{16}$/.test(entry.id) || !/^[a-f0-9]{64}$/.test(entry.sha256) ||
                !['filename', 'name', 'vendor', 'description'].every(key => typeof entry[key] === 'string') ||
                !['appid', 'version', 'size_bytes', 'resource_count'].every(key =>
                    Number.isSafeInteger(entry[key]) && entry[key] >= 0) ||
                !Object.hasOwn(statuses, entry.capture_status) ||
                !Array.isArray(entry.errors) || !Array.isArray(entry.warnings) ||
                (entry.preview !== null && !/^previews\/[a-f0-9]{64}\.png$/.test(entry.preview)) ||
                (entry.preview !== null && entry.capture_status !== 'candidate')) {
                throw new Error('资源条目格式不正确');
            }
            publicURL(entry.download_url);
        }
        return data;
    }
    function selectPage(packages, query, mode, page, pageSize = 24) {
        const terms = query.normalize('NFKC').toLocaleLowerCase().trim().split(/\s+/).filter(Boolean);
        const matches = packages.filter(entry => {
            if (mode === 'previews' && !entry.preview) return false;
            if (mode === 'unavailable' && entry.preview) return false;
            const text = [entry.name, entry.filename, entry.vendor, entry.description, entry.appid, entry.version]
                .join(' ').normalize('NFKC').toLocaleLowerCase();
            return terms.every(term => text.includes(term));
        });
        const pages = Math.max(1, Math.ceil(matches.length / pageSize));
        const current = Math.max(1, Math.min(pages, page));
        return { entries: matches.slice((current - 1) * pageSize, current * pageSize),
            total: matches.length, pages, page: current };
    }
    if (typeof module !== 'undefined' && module.exports) {
        module.exports = { validateCatalog, selectPage };
        return;
    }

    const search = document.getElementById('catalogSearch');
    const filter = document.getElementById('catalogFilter');
    const grid = document.getElementById('catalogGrid');
    const state = document.getElementById('catalogState');
    const pageLabel = document.getElementById('catalogPage');
    const previous = document.getElementById('catalogPrevious');
    const next = document.getElementById('catalogNext');
    let packages = [];
    let page = 1;

    function node(tag, className, text) {
        const element = document.createElement(tag);
        element.className = className;
        if (text !== undefined) element.textContent = text;
        return element;
    }
    function render() {
        const selection = selectPage(packages, search.value, filter.value, page);
        page = selection.page;
        const cards = selection.entries.map(entry => {
            const card = node('article', 'catalog-card');
            card.setAttribute('role', 'listitem');
            const figure = node('div', 'catalog-preview');
            if (entry.preview) {
                const image = node('img', '');
                image.src = './catalog/' + entry.preview;
                image.alt = entry.name + ' 自动截图';
                image.width = 240;
                image.height = 320;
                image.loading = 'lazy';
                image.decoding = 'async';
                image.addEventListener('error', () => {
                    figure.replaceChildren(node('span', 'catalog-image-error', '预览图片加载失败'));
                }, { once: true });
                figure.append(image);
            } else {
                figure.append(node('span', 'catalog-placeholder', '暂未提供预览'));
            }
            const status = node('p', 'catalog-capture-status', statuses[entry.capture_status]);
            const title = node('h3', '', entry.name);
            const vendor = node('p', 'catalog-vendor', entry.vendor || '厂商未注明');
            const description = node('p', 'catalog-description', entry.description || '包内未提供说明');
            const details = node('details', 'catalog-details');
            details.append(node('summary', '', '包信息'));
            details.append(node('p', '', `APPID ${entry.appid} · 版本 ${entry.version}`));
            details.append(node('p', '', `${(entry.size_bytes / 1024).toFixed(1)} KB · ${entry.resource_count} 个资源`));
            details.append(node('p', 'catalog-filename', entry.filename));
            if (entry.errors.length) details.append(node('p', 'catalog-image-error', '包结构校验异常，预览不可用。'));
            const link = node('a', 'catalog-download', '前往原仓库下载 ↗');
            link.href = entry.download_url;
            link.target = '_blank';
            link.rel = 'noopener noreferrer';
            card.append(figure, status, title, vendor, description, details, link);
            return card;
        });
        grid.replaceChildren(...cards);
        state.textContent = `共 ${packages.length} 个包 · 筛选结果 ${selection.total} 个`;
        pageLabel.textContent = `${page} / ${selection.pages}`;
        previous.disabled = page === 1;
        next.disabled = page === selection.pages;
        document.getElementById('catalogEmpty').hidden = selection.total !== 0;
    }
    search.addEventListener('input', () => { page = 1; render(); });
    filter.addEventListener('change', () => { page = 1; render(); });
    previous.addEventListener('click', () => { page -= 1; render(); });
    next.addEventListener('click', () => { page += 1; render(); });
    async function load() {
        try {
            const response = await fetch('./catalog/catalog.json', { cache: 'no-cache' });
            if (!response.ok) throw new Error(`HTTP ${response.status}`);
            const data = validateCatalog(await response.json());
            packages = data.packages;
            document.getElementById('catalogSource').href = data.source_url;
            search.disabled = false;
            filter.disabled = false;
            render();
        } catch (error) {
            console.error('MRP catalog:', error);
            state.textContent = `资源目录加载失败：${error.message}`;
            state.setAttribute('role', 'alert');
        }
    }
    load();
})();
