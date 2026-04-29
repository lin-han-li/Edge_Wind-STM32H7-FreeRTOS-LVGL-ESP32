/**
 * Dashboard JavaScript utilities
 * This file can be used for shared dashboard functionality
 */

// Shared utility functions for dashboard
const DashboardUtils = {
    /**
     * Format date to local string
     */
    formatDate: function(dateString) {
        if (!dateString) return '';
        const date = new Date(dateString);
        // 统一使用北京时间（不受本机时区影响）
        return date.toLocaleString('zh-CN', { timeZone: 'Asia/Shanghai' });
    },

    /**
     * Format number with commas
     */
    formatNumber: function(num) {
        if (num === null || num === undefined) return '0';
        return num.toLocaleString('zh-CN');
    },

    /**
     * Get status color class
     */
    getStatusColor: function(status) {
        const colors = {
            'online': 'success',
            'offline': 'secondary',
            'faulty': 'danger',
            'pending': 'warning',
            'fixed': 'success'
        };
        return colors[status] || 'secondary';
    },

    /**
     * Show toast notification
     */
    showToast: function(message, type = 'info') {
        // Simple alert for now, can be enhanced with a toast library
        const types = {
            'success': '✅',
            'error': '❌',
            'warning': '⚠️',
            'info': 'ℹ️'
        };
        console.log(`${types[type] || ''} ${message}`);
    }
};

