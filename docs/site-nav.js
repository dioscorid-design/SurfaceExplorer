// site-nav.js - barra di navigazione del sito (Home, Gallery, Videos, User
// Manual, Script Guide) per le pagine del manuale e della guida agli script.
//
// Perche' uno script e non la <nav> scritta nell'HTML come in index.html,
// gallery.html e videos.html: queste pagine sono ANCHE il manuale dell'app,
// che le apre da qrc:/docs/ nella finestra Documentation (QTextBrowser).
// Una barra scritta nell'HTML comparirebbe pure li', con Home, Gallery e
// Videos che puntano a pagine assenti dalle risorse (pagina vuota al click).
// QTextBrowser non esegue JavaScript e un <script src> vuoto non ha testo da
// mostrare ne' da trovare con la ricerca del manuale: la barra esiste solo
// nel browser, cioe' sul sito.
//
// Uso: <script src="site-nav.js"></script> nel punto in cui deve stare la
// barra (subito dopo <body>, o in cima al contenitore della pagina). Questo
// file NON va in resources.qrc: all'app non serve.
(function () {
    var me = document.currentScript;
    if (!me) return;

    var page = (location.pathname.split("/").pop() || "").toLowerCase();
    var here = page.indexOf("script_guide") === 0 ? "script_guide.html" : "documentation.html";

    var links = [
        ["index.html", "Home"],
        ["gallery.html", "Gallery"],
        ["videos.html", "Videos"],
        ["documentation.html", "User Manual"],
        ["script_guide.html", "Script Guide"]
    ];

    // Stesso aspetto della <nav> delle altre pagine del sito.
    var css = document.createElement("style");
    css.textContent =
        "nav.site-nav{padding:10px 0 18px;margin-bottom:8px;border-bottom:1px solid #3a3a3a;" +
        "font-family:'Segoe UI',Tahoma,Geneva,Verdana,sans-serif;font-size:15px;line-height:1.6}" +
        "nav.site-nav a{color:#569cd6;text-decoration:none;margin-right:18px}" +
        "nav.site-nav a:hover{text-decoration:underline}" +
        "nav.site-nav a.here{color:#33cc33;font-weight:bold}";
    document.head.appendChild(css);

    var nav = document.createElement("nav");
    nav.className = "site-nav";
    nav.setAttribute("aria-label", "Site");
    for (var i = 0; i < links.length; i++) {
        var a = document.createElement("a");
        a.href = links[i][0];
        a.textContent = links[i][1];
        if (links[i][0] === here) a.className = "here";
        nav.appendChild(a);
    }
    me.parentNode.insertBefore(nav, me);
})();
