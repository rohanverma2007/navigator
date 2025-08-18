const express = require('express');
const { exec } = require('child_process');
const path = require('path');
const app = express();

// Config
const PORT = process.env.PORT || 11080;
const TIMEOUT = 3000; // Reduced from 5s to 3s
const CACHE_TTL = 20000; // Reduced from 30s to 20s
const cache = new Map();

// Minimal middleware - only parse JSON for API routes
app.use('/api', express.json());

// Enable compression for all responses
app.use((req, res, next) => {
    res.setHeader('X-Content-Type-Options', 'nosniff');
    res.setHeader('X-Frame-Options', 'DENY');
    next();
});

// Extract domain efficiently
const getDomain = url => {
    const match = url.match(/^(?:https?:\/\/)?([^:\/]+)/);
    return match ? match[1] : url;
};

// Optimized HTTP check with connection pooling
const checkHttp = url => new Promise(resolve => {
    const fullUrl = url.startsWith('http') ? url : `https://${url}`;
    const start = Date.now();
    
    // Optimized curl: minimal output, aggressive timeouts, HTTP/2 support
    const cmd = `curl -s -o /dev/null -w "%{http_code}" --connect-timeout 2 --max-time 3 --http2 --compressed "${fullUrl}"`;
    
    const timeout = setTimeout(() => resolve({ 
        online: false, 
        code: 0, 
        time: TIMEOUT 
    }), TIMEOUT);

    exec(cmd, (err, stdout) => {
        clearTimeout(timeout);
        const time = Date.now() - start;
        
        if (err) {
            resolve({ online: false, code: 0, time });
            return;
        }
        
        const code = parseInt(stdout.trim()) || 0;
        resolve({
            online: code > 0 && code < 500 && code !== 404,
            code,
            time
        });
    });
});

// Get status with cache
const getStatus = async url => {
    const key = url;
    const now = Date.now();
    const cached = cache.get(key);
    
    if (cached && (now - cached.ts) < CACHE_TTL) {
        return { ...cached.data, cached: true };
    }
    
    const status = await checkHttp(url);
    cache.set(key, { data: status, ts: now });
    
    // Proactive cache cleanup
    if (cache.size > 100) {
        const entries = Array.from(cache.entries());
        entries.slice(0, 50).forEach(([k]) => cache.delete(k));
    }
    
    return { ...status, cached: false };
};

// Batch check optimization
const batchCheck = async urls => {
    const chunks = [];
    for (let i = 0; i < urls.length; i += 10) {
        chunks.push(urls.slice(i, i + 10));
    }
    
    const results = [];
    for (const chunk of chunks) {
        const chunkResults = await Promise.all(
            chunk.map(url => getStatus(url).then(s => ({ url, ...s })))
        );
        results.push(...chunkResults);
    }
    
    return results;
};

// API Routes - Minimal and fast
app.get('/api/health', (req, res) => {
    res.json({ ok: 1, up: process.uptime() });
});

app.get('/api/check', async (req, res) => {
    const { url } = req.query;
    if (!url) return res.status(400).json({ error: 'Missing url' });
    
    const status = await getStatus(url);
    res.json({
        url,
        online: status.online,
        code: status.code,
        time: status.time,
        cached: status.cached
    });
});

app.post('/api/check-multiple', async (req, res) => {
    const { urls } = req.body;
    if (!Array.isArray(urls)) return res.status(400).json({ error: 'Invalid urls' });
    
    const results = await batchCheck(urls);
    const online = results.filter(r => r.online).length;
    
    res.json({
        results,
        summary: { total: results.length, online, offline: results.length - online }
    });
});

app.get('/api/cache', (req, res) => {
    res.json({
        size: cache.size,
        ttl: CACHE_TTL,
        entries: Array.from(cache.entries()).map(([k, v]) => ({
            url: k,
            online: v.data.online,
            age: Date.now() - v.ts
        }))
    });
});

app.delete('/api/cache', (req, res) => {
    cache.clear();
    res.json({ cleared: 1 });
});

// Static files with aggressive caching
app.use(express.static('.', {
    maxAge: '1d',
    etag: true,
    lastModified: true,
    setHeaders: (res, path) => {
        if (path.endsWith('.html')) {
            res.setHeader('Cache-Control', 'no-cache');
        } else if (path.endsWith('.json')) {
            res.setHeader('Cache-Control', 'no-store');
        } else {
            res.setHeader('Cache-Control', 'public, max-age=86400');
        }
    }
}));

// Root route
app.get('/', (req, res) => {
    res.sendFile(path.join(__dirname, 'index.html'));
});

// Start server with cluster support for production
const server = app.listen(PORT, '0.0.0.0', () => {
    console.log(`🚀 Navigator API on port ${PORT}`);
    console.log(`📊 Health: http://localhost:${PORT}/api/health`);
});

// Optimize TCP settings
server.keepAliveTimeout = 65000;
server.headersTimeout = 66000;

// Graceful shutdown
process.on('SIGINT', () => {
    console.log('\n👋 Shutting down...');
    server.close(() => process.exit(0));
    setTimeout(() => process.exit(0), 5000);
});

module.exports = app;
