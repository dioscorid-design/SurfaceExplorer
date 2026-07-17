# SurfaceExplorer — regole per il codice

## Recorder video (videorecorder.cpp)
- Contratto del loop di registrazione: **il frame i mostra ciò che mostrerebbe lo schermo al tempo equivalente**. Mai duplicare logica live nel recorder: le copie divergono ed è la famiglia di bug più ricorrente del progetto (moduli stoppati che ripartivano nel video, vista path sbagliata, base 4D ignorata).
- La camera dei path passa SOLO da `MainWindow::applyPath3DCameraAt(t)` / `applyPath4DCameraAt(t)`; le rotazioni (oggetto + 4D) SOLO da `GLWidget::advanceRotationsBy(dt)` — uniche implementazioni condivise tra tick live e loop di registrazione (il recorder passa tempo/dt virtuali del frame). Ogni nuovo comportamento di camera/moto va estratto allo stesso modo: funzione parametrizzata sul tempo, chiamata da entrambi i punti.
- Nei diff che toccano videorecorder.cpp, cercare stato live ricostruito a mano (velocità, modalità di vista, clock): è il segnale d'allarme.

## Nomi trappola
- `m_pathViewMode4D` = vista del path 4D (pushView); `m_pathViewMode3D` = vista del path 3D (pushView3D). Non abbreviarli né confonderli: il bug storico nasceva dai vecchi nomi quasi identici (`m_pathMode`/`m_pathMode3D`).
- Le chiavi JSON di persistenza restano `"pathMode"` / `"pathMode3D"` (compatibilità coi record esistenti).
