// INFINIUM: Marketplace tab. Shows marketplace.json as brand cards: the brands of your installed printers first,
// then free model sites, then the other brands. Search filters brands and links.
//
// In the app: the app calls MarketplaceSetData(data, vendors) when the page has loaded; the page also asks with
// {"command":"marketplace_get"} once the "wx" bridge is there (on Linux it arrives a moment after loading starts).
// Clicking a link sends {"command":"marketplace_open","url":…} and the app opens it in the system browser (only
// links that are in marketplace.json). Without the bridge the link's own target=_blank is used; the app sends
// that new window to the system browser too.
// In a normal browser (tests): marketplace.json is read next to this page and links open in a new tab;
// ?vendors=BBL,Snapmaker pretends those printers are installed.
(function () {
	'use strict';

	var state = { data: null, vendors: [], query: '', fromApp: false };

	var SECTIONS = [
		{ key: 'apps', label: 'Apps', icon: '<rect x="3" y="4" width="18" height="16" rx="2"/><path d="M3 9h18"/>' },
		{ key: 'models', label: 'Free models', icon: '<path d="M12 2 3 7v10l9 5 9-5V7z"/><path d="m3 7 9 5 9-5M12 12v10"/>' },
		{ key: 'help', label: 'Help & firmware', icon: '<circle cx="12" cy="12" r="9"/><circle cx="12" cy="12" r="4"/><path d="m5.6 5.6 3.6 3.6m5.6 5.6 3.6 3.6m0-12.8-3.6 3.6m-5.6 5.6-3.6 3.6"/>' }
	];
	var OUT_ICON = '<svg class="mk-out" viewBox="0 0 24 24" aria-hidden="true"><path d="M15 3h6v6M10 14 21 3M18 13v6a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h6"/></svg>';
	var PHONE_ICON = '<svg viewBox="0 0 24 24" aria-hidden="true"><rect x="6" y="2" width="12" height="20" rx="2"/><path d="M11 18h2"/></svg>';
	var STAR_ICON = '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="m12 2 3.1 6.3 6.9 1-5 4.9 1.2 6.8L12 17.8 5.8 21l1.2-6.8-5-4.9 6.9-1z"/></svg>';

	// The app's message bridge (window.wx), or null in a normal browser / before the app has added it.
	function bridge() {
		return window.wx && typeof window.wx.postMessage === 'function' ? window.wx : null;
	}

	function inApp() {
		return state.fromApp || /BBL-Slicer/i.test(navigator.userAgent);
	}

	function send(obj) {
		var wx = bridge();
		if (wx) wx.postMessage(JSON.stringify(obj));
		return !!wx;
	}

	// The app shows dark mode by putting "dark" in the user agent (like the home page).
	function applyTheme() {
		if (inApp()) document.documentElement.setAttribute('data-theme', /\(dark\)/i.test(navigator.userAgent) ? 'dark' : 'light');
	}

	function esc(s) {
		return String(s == null ? '' : s).replace(/[&<>"']/g, function (c) {
			return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c];
		});
	}

	function norm(s) {
		return String(s || '').toLowerCase().normalize('NFKD').replace(/[̀-ͯ]/g, '');
	}

	function linkText(l) {
		return norm(l.name + ' ' + l.text + (l.android ? ' android phone' : '') + (l.iphone ? ' iphone ios phone' : ''));
	}

	function matches(hay, words) {
		for (var i = 0; i < words.length; i++) if (hay.indexOf(words[i]) < 0) return false;
		return true;
	}

	// The brand with only the links that match, or null. A brand whose own name matches keeps all its links.
	function filterBrand(b, words) {
		if (!words.length) return b;
		var brandText = norm(b.name + ' ' + b.vendors.join(' ') + ' ' + b.about);
		if (matches(brandText, words)) return b;
		var pick = function (links) { return links.filter(function (l) { return matches(brandText + ' ' + linkText(l), words); }); };
		var out = {};
		for (var k in b) out[k] = b[k];
		out.apps = pick(b.apps); out.models = pick(b.models); out.help = pick(b.help);
		return out.apps.length || out.models.length || out.help.length ? out : null;
	}

	function isMine(b) {
		for (var i = 0; i < b.vendors.length; i++) if (state.vendors.indexOf(b.vendors[i]) >= 0) return true;
		return false;
	}

	function a(href, cls, inner, label) {
		return '<a href="' + esc(href) + '" target="_blank" rel="noopener noreferrer" class="' + cls + '" data-out="1"'
			+ (label ? ' aria-label="' + esc(label) + '"' : '') + '>' + inner + '</a>';
	}

	function linkRow(l) {
		var phone = '';
		if (l.android || l.iphone) {
			phone = '<span class="mk-phone">' + PHONE_ICON
				+ (l.android ? a(l.android, 'mk-chip android', 'Android', l.name + ' for Android') : '')
				+ (l.iphone ? a(l.iphone, 'mk-chip iphone', 'iPhone', l.name + ' for iPhone') : '')
				+ '</span>';
		}
		return '<li class="mk-link">'
			+ a(l.url, 'mk-link-main', '<span class="mk-link-name">' + esc(l.name) + OUT_ICON + '</span><span class="mk-link-text">' + esc(l.text) + '</span>')
			+ phone + '</li>';
	}

	function brandCard(b, mine) {
		var html = '<article class="mk-card' + (mine ? ' mine' : '') + '" style="--brand:' + esc(b.color) + '" data-brand="' + esc(b.id) + '">'
			+ '<header><span class="mk-badge" aria-hidden="true">' + esc(b.badge) + '</span><div class="mk-card-title"><h3>' + esc(b.name)
			+ (mine ? '<em class="mk-mine">' + STAR_ICON + 'Your printer</em>' : '')
			+ (b.community ? '<em class="mk-community">Community</em>' : '')
			+ '</h3><span>' + esc(b.about) + '</span></div></header>';
		SECTIONS.forEach(function (s) {
			if (!b[s.key].length) return;
			html += '<section class="mk-section"><h4><svg viewBox="0 0 24 24" aria-hidden="true">' + s.icon + '</svg>' + s.label + '</h4><ul>'
				+ b[s.key].map(linkRow).join('') + '</ul></section>';
		});
		return html + '</article>';
	}

	function longDate(iso) {
		var d = new Date(iso + 'T12:00:00');
		if (isNaN(d.getTime())) return iso;
		var months = ['January', 'February', 'March', 'April', 'May', 'June', 'July', 'August', 'September', 'October', 'November', 'December'];
		return d.getDate() + ' ' + months[d.getMonth()] + ' ' + d.getFullYear();
	}

	function render() {
		var box = document.getElementById('mkContent');
		if (!state.data) return;
		var words = norm(state.query).split(/\s+/).filter(Boolean);
		var brands = state.data.brands.map(function (b) { return filterBrand(b, words); }).filter(Boolean);
		var mine = brands.filter(isMine), others = brands.filter(function (b) { return !isMine(b); });
		var sites = state.data.models.filter(function (m) { return matches(linkText(m), words); });
		var html = '';
		if (!brands.length && !sites.length) {
			html += '<div class="mk-empty">Nothing found for “' + esc(state.query) + '”. Try a brand name, like <b>Creality</b>, or a word like <b>firmware</b>.</div>';
		}
		if (mine.length) {
			html += '<section class="mk-group" aria-label="Your printers\' brands"><h2>Your printers\' brands</h2><div class="mk-grid">'
				+ mine.map(function (b) { return brandCard(b, true); }).join('') + '</div></section>';
		}
		if (sites.length) {
			html += '<section class="mk-group" aria-label="Free models"><h2>Free models</h2><p class="mk-note">Find something fun to print. These sites have lots of free models.</p><ul class="mk-sites">'
				+ sites.map(function (m) {
					return '<li>' + a(m.url, 'mk-site', '<span class="mk-site-dot" style="background:' + esc(m.color) + '" aria-hidden="true">' + esc(m.name.charAt(0))
						+ '</span><span class="mk-site-text"><b>' + esc(m.name) + OUT_ICON + '</b><span>' + esc(m.text) + '</span></span>') + '</li>';
				}).join('') + '</ul></section>';
		}
		if (others.length) {
			var title = mine.length ? 'More printer brands' : 'Printer brands';
			html += '<section class="mk-group" aria-label="' + title + '"><h2>' + title + '</h2><div class="mk-grid">'
				+ others.map(function (b) { return brandCard(b, false); }).join('') + '</div></section>';
		}
		box.innerHTML = html;
		document.getElementById('mkFoot').textContent = 'Only official links: no shops, no ads, nothing tracks you. Links checked on '
			+ longDate(state.data.checked) + '. ' + (inApp() ? 'Links open in your web browser.' : 'Links open in a new tab.');
	}

	// Called by the app (MarketplacePanel.cpp).
	window.MarketplaceSetData = function (data, vendors) {
		state.fromApp = true;
		state.data = data;
		state.vendors = vendors || [];
		applyTheme();
		render();
	};
	window.MarketplaceSetVendors = function (vendors) {
		state.vendors = vendors || [];
		render();
	};

	function loadInBrowser() {
		var q = /[?&]vendors=([^&]*)/.exec(location.search);
		var vendors = q ? decodeURIComponent(q[1]).split(',').filter(Boolean) : [];
		var xhr = new XMLHttpRequest();
		xhr.open('GET', 'marketplace.json');
		var failed = function () {
			if (!state.data) document.getElementById('mkContent').innerHTML = '<div class="mk-empty">The list could not be read.</div>';
		};
		xhr.onload = function () {
			if (state.fromApp) return; // the app's data wins
			try {
				state.data = JSON.parse(xhr.responseText);
				state.vendors = vendors;
				render();
			} catch (e) { failed(); }
		};
		xhr.onerror = failed;
		xhr.send();
	}

	document.addEventListener('DOMContentLoaded', function () {
		applyTheme();
		setInterval(applyTheme, 1000);
		var search = document.getElementById('mkSearch'), clear = document.getElementById('mkClear');
		search.addEventListener('input', function () {
			state.query = search.value;
			clear.hidden = !search.value;
			render();
		});
		clear.addEventListener('click', function () {
			search.value = ''; state.query = ''; clear.hidden = true; render(); search.focus();
		});
		// In the app, every outside link goes to the system browser.
		document.addEventListener('click', function (e) {
			var el = e.target.closest ? e.target.closest('a[data-out]') : null;
			if (!el || !bridge()) return;
			e.preventDefault();
			send({ command: 'marketplace_open', url: el.getAttribute('href') });
		});
		// Ask the app as soon as its bridge is there; read the file ourselves if no app answers.
		var tries = 0;
		(function ask() {
			if (state.fromApp) return;
			if (send({ command: 'marketplace_get' })) return;
			if (++tries < 30) setTimeout(ask, 100);
		})();
		setTimeout(function () { if (!state.data) loadInBrowser(); }, 1200);
	});
})();
