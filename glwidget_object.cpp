// glwidget_object.cpp - Le FASI DEL FRAME di un oggetto della scena (2.0).
// render() (glwidget.cpp) fa la regia: watchdog, camera, orologi, sfondo,
// apertura e chiusura del render pass; qui cio' che si ripete per ogni
// oggetto, nell'ordine in cui render() lo chiama:
//   prepareObjectUniforms  posa, blocchi UBO delle parti e del bordo
//   prepareObjectTexture   scarico/caricamento della texture di superficie
//   prepareObjectGeometry  pipeline e buffer (mesh, lati, bordo, wireframe)
//   runObjectCompute       tubi e lati coperti, fuori dal render pass
//   drawObject             il disegno nel render pass, in tre fasi (opachi,
//                          trasparenti, linee) che render() fa per tutti gli oggetti
//   drawFlatObject         l'anteprima 2D della sua texture (o di quella del bordo)
// Tappa 1b: i blocchi sono quelli di render(), spostati cosi' come erano.

#include "glwidget.h"

#include <QColor>
#include <algorithm>
#include <cmath>
#include <cstddef>

void GLWidget::prepareObjectUniforms(SurfaceObject &o, QRhiResourceUpdateBatch *resourceUpdates)
{
    // UNIFORM PROPRI dell'oggetto che non cambiano a ogni frame (costanti,
    // dominio, taglio): dai suoi campi nella copia di lavoro.
    m_uboData.mathParams = o.mathParams;
    m_uboData.mathParams2 = o.mathParams2;
    m_uboData.u_min = o.uMin; m_uboData.u_max = o.uMax;
    m_uboData.v_min = o.vMin; m_uboData.v_max = o.vMax;
    m_uboData.x_min = o.xMin; m_uboData.x_max = o.xMax;
    m_uboData.y_min = o.yMin; m_uboData.y_max = o.yMax;
    m_uboData.z_min = o.zMin; m_uboData.z_max = o.zMax;
    // Strisce del wireframe (catture di immagini): meta' spessore e dimensione, in pixel.
    m_uboData.u_wireHalfWidth = 0.5f * float(m_captureLineWidth);
    m_uboData.u_wireViewW = float(renderTarget()->pixelSize().width());
    m_uboData.u_wireViewH = float(renderTarget()->pixelSize().height());

    // POSA DELL'OGGETTO: il modello e' suo, proiezione e vista della camera.
    QMatrix4x4 mvp;
    QMatrix4x4 mv;
    if (m_isFlatView) {
        o.model.setToIdentity();
        mvp = rhi()->clipSpaceCorrMatrix();
        mv.setToIdentity();
    } else {
        o.model.setToIdentity();
        // Gli oggetti girano con l'ambiente attorno al centro della scena
        // (sceneRotation), posizione compresa: la rotazione va prima della posa.
        if (!isEnvironment(o)) o.model.rotate(m_sceneRotation.rotation);
        // La posizione piu' il path dell'oggetto (timeline 2a) al suo tempo.
        o.model.translate(effectivePosition(o));
        o.model.rotate(o.rotationQuat);
        if (o.scale != 1.0f) o.model.scale(o.scale);   // a 1 la matrice di sempre

        mvp = rhi()->clipSpaceCorrMatrix() * m_projection * m_view * o.model;
        mv = m_view * o.model;
    }

    // --- 3. COMPILAZIONE UNIFORM BUFFER (UBO) ---
    memcpy(m_uboData.mvpMatrix, mvp.constData(), 64);
    memcpy(m_uboData.mvMatrix, mv.constData(), 64);
    memcpy(m_uboData.mMatrix, o.model.constData(), 64);

    m_uboData.dummyZero = QVector4D(0.0f, 0.0f, 0.0f, 0.0f);
    m_uboData.observerPos = m_observerPos;
    m_uboData.cameraPos4D = m_cameraPos4D;
    // Quelli che l'oggetto MOSTRA: per gli oggetti della scena c'e' anche la
    // rotazione del path (vedi GLWidget::setViewRotation4D), e quella data
    // dall'ambiente (sceneRotation), che i suoi angoli non contengono.
    QVector3D angles4D = shownAngles4D(o);
    if (!isEnvironment(o)) angles4D += m_sceneRotation.angles4D;
    // OGGETTO RIGIDO (ambiente in ray marching, vedi rigidObject): si muove
    // coi soli moti 3D. Con angoli e w a zero la proiezione 4D -> 3D lascia i
    // punti dove sono, in ogni modalita'.
    const bool rigid = rigidObject(o);
    if (rigid) angles4D = QVector3D();
    m_uboData.omega = angles4D.x();
    m_uboData.phi = angles4D.y();
    m_uboData.psi = angles4D.z();

    // 3. INVIO DEI DATI ALLA GPU
    const ClockTimes clocks = clockTimes(o);
    m_uboData.time = clocks.geom;

    // Usiamo la coordinata X di dummyZero per inviare il tempo specifico della Texture
    m_uboData.dummyZero.setX(clocks.tex);

    // .y = flag "seconda superficie interna" (Inner:= nello script ray marching).
    // .x resta l'orologio texture; .y/.z/.w erano liberi (azzerati a inizio frame).
    m_uboData.dummyZero.setY(o.raymarchHasInner ? 1.0f : 0.0f);
    // .z = quota del piano di sezione (Cross Section). Si riusa dummyZero invece
    // di aggiungere un campo all'UBO: un campo nuovo andrebbe dichiarato in
    // ENTRAMBI gli shader o Adreno rifiuta il link (vedi CLAUDE.md), e qui basta
    // una componente gia' presente e inutilizzata.
    m_uboData.dummyZero.setZ(m_crossSectionP);
    // .w = P(t) del path dell'oggetto (scene 4D): il vertex la somma alla w dei
    // punti ruotati, prima della proiezione (rotate4D). Ultima componente
    // libera di dummyZero, stessa ragione della .z: nessun campo nuovo.
    m_uboData.dummyZero.setW(rigid ? 0.0f : pathOffset(o).w());
    m_uboData.projMode = projectionMode;
    m_uboData.renderMode = o.renderMode;
    m_uboData.lightingMode = is4DActive(o) ? m_lightingMode4D : 0;
    m_uboData.useSpecular = o.isSpecularEnabled ? 1 : 0;

    m_uboData.isFlat = m_isFlatView ? 1 : 0;
    // TRASFORMAZIONE 2D GLOBALE (non il buffer di lavoro della vista 2D): e' la
    // base di OGNI blocco parte, quindi ci va lo stato globale, o l'inquadratura
    // che il mouse sta dando alla fascia in editing si propagherebbe a tutte le
    // fasce che ereditano e all'ambito "All".
    // In vista 2D sulla SUPERFICIE fa eccezione cio' che si sta editando: li' il
    // fragment deve seguire il mouse.
    //  - con una FASCIA selezionata ci pensa il ramo editingThisPart del loop
    //    per-parte piu' sotto, che scrive il buffer nel blocco di quella parte;
    //  - con una mesh SOLA (nessuna parte dichiarata) quel loop non esiste;
    //  - in ambito "ALL" (nessuna parte attiva) nessuna parte e' "in editing",
    //    e il quad 2D disegna col blocco 0.
    // Negli ultimi due casi il buffer va messo QUI, o trascinare non muove nulla.
    const bool flatEditingGlobal = m_isFlatView && m_flatViewTarget == 0
                                   && (!o.engine || o.engine->getMeshPartCount() <= 1
                                       || o.activeMeshPart < 0);
    m_uboData.zoom     = flatEditingGlobal ? m_flatZoom     : o.globalTexZoom;
    m_uboData.center   = flatEditingGlobal ? m_flatPan      : o.globalTexPan;
    m_uboData.rotation = flatEditingGlobal ? m_flatRotation : o.globalTexRotation;

    if (o.engine) {
        m_uboData.hasExplicitW = (!o.engine->getActiveExplicitEquation().isEmpty()) ? 1 : 0;
    } else {
        m_uboData.hasExplicitW = 0;
    }

    m_uboData.u_raySteps = o.raySteps;

    if (o.textureEnabled && o.engineMode == ModeParametric) {
        m_uboData.color = QVector3D(1.0f, 1.0f, 1.0f);
    } else {
        m_uboData.color = QVector3D(o.red, o.green, o.blue);
    }

    // FALLBACK ANTI-SPARIZIONE: su un campo implicito mal condizionato (prodotto,
    // es. "Chain") il ramo trasparente aggancia crossing fantasma e la superficie
    // sparirebbe con alpha<1. Forziamo opaco: il cammino opaco (d=val/gradLen) e'
    // affidabile. La UI disabilita comunque lo slider, ma questo e' l'ultimo baluardo
    // indipendente da come alpha e' stato impostato (slider/preset/script).
    m_uboData.alpha = (o.engineMode == ModeImplicit && o.implicitIllConditioned) ? 1.0f : o.alpha;
    m_uboData.lightIntensity = o.lightIntensity;

    m_uboData.col1 = QVector3D(o.texRed1, o.texGreen1, o.texBlue1);
    m_uboData.col2 = QVector3D(o.texRed2, o.texGreen2, o.texBlue2);
    m_uboData.useTexture = o.textureEnabled ? 1 : 0;

    // NESSUNA IMMAGINE nello slot sampler: gli script che campionano iChannel0
    // (famiglia "Animated Images") cadono sulla scacchiera procedurale invece
    // che sui pixel della texture tappabuchi. surfaceTexture e' la texture
    // dell'immagine caricata: nullo = non c'e' immagine.
    // Conta l'immagine dell'UTENTE, non "esiste una texture": generateTexture()
    // carica la scacchiera di default per la stessa via, quindi surfaceTexture
    // resta popolata anche senza alcuna immagine e il fallback non sarebbe mai
    // scattato (era il motivo per cui gli slider colore sembravano inerti).
    m_uboData.u_noImage = o.surfaceImagePath.isEmpty() ? 1 : 0;

    // Luce di riempimento dall'osservatore: 0 (default) = termine nullo nello
    // shader, immagine identica a quella storica. Vedi u_fillLight in UboData.
    m_uboData.u_fillLight = m_fillLight;

    // Spessore del guscio (modalita' Shell): uniform, non entra nel sorgente
    // dello shader, quindi cambiarlo non richiede una ricompilazione.
    m_uboData.u_shellThickness = o.shellThickness;

    // Scelta del marcher (radio Ray Marching): come lo spessore, e' un uniform,
    // quindi il cambio e' immediato senza ricompilare lo shader.
    m_uboData.u_marcherMode = o.hybridMarcher ? 1.0f : 0.0f;

    // Raggio del tubo (sotto-tab Tubes): uniform anche lui, lo slider dello
    // spessore e' immediato.
    m_uboData.u_tubeRadius = o.tubeRadius;
    m_uboData.u_borderRadius = o.borderRadius;
    m_uboData.u_isBorder = 0;   // solo i blocchi del bordo lo accendono

    // ==========================================================
    // AGGIORNAMENTO BUFFER PRINCIPALE (multi-mesh)
    // ==========================================================
    // Scriviamo un blocco UBO per parte di mesh: tutto identico tranne i limiti
    // del dominio (u_min/u_max/v_min/v_max) e u_meshIndex, che sono per-parte.
    // Il blocco 0 resta quello "globale": lo usano lo sfondo in flat view e ogni
    // draw che non appartiene a una parte, quindi il caso a mesh singola scrive
    // esattamente cio' che scriveva prima.
    const std::vector<MeshPart> &uboParts = o.engine->getMeshParts();
    const int uboPartCount = (int)uboParts.size();

    // TUBO: direzione della sezione e spessore del tubo `mesh` (vedi
    // SurfaceEngine::TubeCopy e MeshPart::tubeThickness, fattore sullo
    // spessore del campo Thickness). Fuori dai tubi nessuno li legge.
    // Sezione trasportata: gli anelli della parte, e se la passata compute di
    // questo frame scrive la sua riga (entro i limiti della texture).
    const bool tubeFrames = tubeFramesActive(o);
    // Smorzamento della sezione: costante di tempo 0.1 s, indipendente dai fps.
    const float tubeBlend = 1.0f - std::exp(-std::max(0.0f, o.tubeFrameDt) / 0.1f);
    auto applyTubeFields = [&o, tubeFrames, tubeBlend](UboData &u, const MeshPart *mp, int mesh) {
        const QVector3D r = o.engine->tubeReference(mesh);
        u.u_tubeRefX = r.x();
        u.u_tubeRefY = r.y();
        u.u_tubeRefZ = r.z();
        u.u_tubeRadius = o.tubeRadius * ((mp && mp->tubeThickness >= 0.0f) ? mp->tubeThickness : 1.0f);
        const int rings = mp ? mp->numU + 1 : 0;
        u.u_tubeRings = float(rings);
        u.u_tubeFramesOn = (tubeFrames && rings >= 2 && rings <= kTubeFrameMaxRings
                            && mesh >= 0 && mesh < kTubeFrameMaxParts) ? 1.0f : 0.0f;
        u.u_tubeFrameBlend = tubeBlend;
    };
    applyTubeFields(m_uboData, uboParts.empty() ? nullptr : &uboParts[0], 0);
    m_uboData.u_meshCount = (float)std::max(1, uboPartCount);
    // Col bordo acceso i blocchi raddoppiano: dopo quelli delle parti, uno di
    // bordo per parte (vedi sotto).
    const bool borderOn = (o.borderRadius > 0.0f && o.engineMode == ModeParametric);
    const int uboPartSlots = std::max(1, uboPartCount);

    // Cresce l'UBO se lo script ha dichiarato piu' parti della capienza attuale.
    // ensureUboCapacity riaggancia i binding in place (senza distruggerli),
    // quindi le pipeline restano valide e non serve ricostruirle.
    // Nel parametrico, dopo i blocchi del bordo, i cinque della passata dei
    // lati coperti per ogni istante della sonda: anche a bordo spento, che la
    // usa come sonda (borderAvailability).
    ensureUboCapacity(o, o.engineMode == ModeParametric ? uboPartSlots * 2 + 5 * kBorderProbeInstants : uboPartSlots);
    ensureDynamicBindings(o, o.surfaceTexture ? o.surfaceTexture : m_dummyTexture);
    // Immagini proprie delle fasce: prima dei blocchi, che ne dipendono (u_noImage).
    syncPartImages(o, resourceUpdates);

    if (uboPartCount <= 1) {
        // Mesh singola: un blocco solo, come da sempre.
        resourceUpdates->updateDynamicBuffer(o.ubo, 0, sizeof(UboData), &m_uboData);
    } else {
        for (int k = 0; k < uboPartCount; ++k) {
            const MeshPart &mp = uboParts[k];
            UboData partUbo = m_uboData;
            partUbo.u_min = mp.uMin;
            partUbo.u_max = mp.uMax;
            partUbo.v_min = mp.vMin;
            partUbo.v_max = mp.vMax;
            partUbo.u_meshIndex = (float)mp.meshIndex;
            applyTubeFields(partUbo, &mp, mp.meshIndex);
            partUbo.u_globalTexture = globalTextureActive(o) ? 1.0f : 0.0f;

            // ASPETTO PER-PARTE. Un valore negativo significa "eredita dallo
            // stato globale", che e' gia' in partUbo perche' copiato da
            // m_uboData: percio' una parte non configurata resta identica a
            // prima e nessun preset esistente cambia.
            // Il colore NON si applica con una texture attiva: li' il motore
            // forza deliberatamente ubuf.color a bianco per non sporcare le
            // texture che portano gia' il proprio colore (vedi il ramo
            // textureEnabled sopra), e scriverci sopra lo vanificherebbe.
            // AMBITO "ALL" (meshAppearanceUniform): l'aspetto proprio viene
            // IGNORATO e resta valido solo il globale, gia' in partUbo. La
            // superficie si disegna come una sola; i valori per-parte non
            // vengono toccati, quindi tornando su "Mesh" ricompaiono.
            if (!o.meshAppearanceUniform) {
                // Questa parte finira' texturizzata? Serve PRIMA del colore: una
                // parte SENZA texture deve riprendersi la propria tinta, mentre
                // una texturizzata vuole il bianco neutro (finalRGB = color * tex).
                //
                // IN WIREFRAME (o Bordi) LA TEXTURE NON SI APPLICA: il fragment
                // esce subito con `FragColor = vec4(ubuf.color, alpha)` (vedi la
                // guardia u_renderMode 2/5 in surface.frag), quindi ubuf.color
                // NON e' un fattore da neutralizzare ma il colore FINALE delle
                // linee. Trattare la parte come texturizzata la disegnava col
                // bianco neutro: mettendo il wireframe su una mesh con texture,
                // le linee diventavano bianche e il colore proprio spariva.
                // Stesso criterio del render (effectivePartRenderMode), cosi' le
                // due decisioni non possono divergere.
                const int partMode = effectivePartRenderMode(o, mp);
                const bool partFlatColored = (partMode == 2 || partMode == 5);
                const bool partTextured = mp.effectiveTextureEnabledMulti()
                                          && !partFlatColored;

                // COLORE DELLA PARTE. Il ramo globale poco sopra forza
                // m_uboData.color a BIANCO quando la texture globale e' accesa,
                // e ogni blocco parte da quella copia: una fascia senza texture
                // si ritrovava quindi bianca invece che del proprio colore
                // (o di quello globale). Il colore va soppresso solo se e'
                // QUESTA parte ad essere texturizzata, non perche' lo e' la
                // superficie.
                if (!partTextured) {
                    // Tinta piena: la propria se ce l'ha, altrimenti quella
                    // globale REALE (red/green/blue), non il bianco neutro che
                    // m_uboData porta quando la texture globale e' accesa.
                    if (mp.hasCustomColor())
                        partUbo.color = QVector3D(mp.colorR, mp.colorG, mp.colorB);
                    else if (o.textureEnabled && o.engineMode == ModeParametric)
                        partUbo.color = QVector3D(o.red, o.green, o.blue);
                }
                if (mp.alpha >= 0.0f)
                    partUbo.alpha = mp.alpha;
                // LUCE: NON per-parte. E' l'illuminazione della scena (come la
                // speculare) e vale per tutta la superficie, qualunque mesh sia
                // selezionata: partUbo porta gia' il valore globale.
                // Il campo lightIntensity resta in MeshPart per compatibilita'
                // con i preset che lo hanno salvato -- viene letto e riscritto
                // dalla persistenza -- ma non governa piu' il render.

                // TEXTURE PROPRIA DELLA PARTE. Il codice e' gia' compilato nello
                // shader come getCustomColor_<k> (vedi createFragmentShaderSource):
                // qui basta accendere l'interruttore per questa parte, perche' il
                // dispatcher sceglie la funzione da u_meshIndex.
                //
                // TEXTURE EFFICACE DELLA PARTE. Si scrive SEMPRE, anche quando
                // la parte non ne ha una propria: in multi-mesh una fascia mai
                // configurata deve restare SENZA texture, non ereditare quella
                // globale (vedi effectiveTextureEnabledMulti). Prima qui si
                // entrava solo con hasCustomTexture e le altre si tenevano lo
                // useTexture globale copiato da m_uboData: applicando una
                // texture in "All" le fasce lasciate apposta nude se la
                // prendevano addosso.
                partUbo.useTexture = partTextured ? 1 : 0;

                // IMMAGINE PROPRIA DELLA PARTE. Se il suo script nomina
                // un'immagine (tag //IMG:) e quella e' caricata, allo slot 1 di
                // QUESTA draw call c'e' la sua texture (bindingsForPart), non
                // quella della superficie: la leggono la funzione di default e
                // gli script che campionano iChannel0. u_noImage va quindi a 0
                // anche se la superficie non ha alcuna immagine.
                // Nominata ma non caricabile: resta tutto com'e' in m_uboData,
                // cioe' l'immagine della superficie se c'e', altrimenti la
                // scacchiera di ripiego.
                if (partImageFor(o, mp))
                    partUbo.u_noImage = 0;

                if (mp.hasCustomTexture) {

                    // COLORE BASE A BIANCO SULLA PARTE TEXTURIZZATA.
                    // Il fragment compone finalRGB = ubuf.color * texture: il
                    // colore va quindi neutralizzato, o la texture esce
                    // MOLTIPLICATA per la tinta della superficie (con un colore
                    // scuro il risultato e' nero, e con la scacchiera meta'
                    // celle si annullano -> la mesh sembra sparire).
                    // Il ramo globale lo fa gia' piu' sopra, ma solo quando e'
                    // textureEnabled GLOBALE ad essere acceso: una texture
                    // per-mesh non passa di li'.
                    // NB: si scrive DOPO il colore proprio della parte, cosi'
                    // vince su di esso; il colore resta in MeshPart e ricompare
                    // spegnendo la texture.
                    // Stessa condizione usata per useTexture e per il colore
                    // pieno qui sopra: il bianco neutro e la texture accesa
                    // devono essere la STESSA decisione, o si torna al caso
                    // "parte bianca senza texture".
                    if (partTextured)
                        partUbo.color = QVector3D(1.0f, 1.0f, 1.0f);

                    // COLORI u_col1/u_col2 DELLA PARTE. Vivono nel blocco UBO,
                    // quindi possono essere diversi per ogni mesh: senza questo
                    // le parti condividevano i due slot globali e l'ultima
                    // texture applicata riscriveva i colori di TUTTE le altre.
                    if (mp.hasCustomTexColors()) {
                        partUbo.col1 = QVector3D(mp.texCol1R, mp.texCol1G, mp.texCol1B);
                        partUbo.col2 = QVector3D(mp.texCol2R, mp.texCol2G, mp.texCol2B);
                    }

                }

                // ZOOM / PAN / ROTAZIONE 2D PROPRI DELLA PARTE. Stessa ragione
                // dei colori: sono campi del blocco UBO, quindi per-parte.
                // Senza, la trasformazione fatta col mouse in vista 2D su UNA
                // texture veniva applicata a tutte.
                // FUORI dal blocco hasCustomTexture: la trasformazione e' una
                // proprieta' della PARTE, non dello script. Due fasce con LA
                // STESSA texture devono poterla mostrare a scale diverse, e una
                // fascia che eredita la texture globale deve avere comunque la
                // propria inquadratura, altrimenti resta agganciata allo zoom
                // globale e si ridimensiona insieme alle altre.
                // In vista 2D (m_isFlatView) si edita a schermo intero la
                // texture della SOLA parte attiva: quella deve seguire i membri
                // globali che il mouse muove (o trascinare non muoverebbe
                // nulla), mentre tutte le ALTRE restano ferme sulla propria.
                const bool editingThisPart = m_isFlatView && m_flatViewTarget == 0
                                             && (k == o.activeMeshPart);
                if (editingThisPart) {
                    // BUFFER DI LAVORO, esplicito. Prima qui si lasciava stare,
                    // contando sul fatto che partUbo (copia di m_uboData) gia'
                    // portasse i membri che il mouse muove. Non e' piu' vero: in
                    // m_uboData ora c'e' la trasformazione GLOBALE persistente,
                    // che il mouse non tocca -- e la texture in editing restava
                    // immobile al trascinamento.
                    partUbo.zoom = m_flatZoom;
                    partUbo.center = m_flatPan;
                    partUbo.rotation = m_flatRotation;
                } else if (mp.hasCustomTexTransform()) {
                    partUbo.zoom = mp.texZoom;
                    partUbo.center = QVector2D(mp.texPanX, mp.texPanY);
                    partUbo.rotation = mp.texRotation;
                }

                // OROLOGIO DELLA PARTE (dummyZero.x = tempo letto da ogni
                // getCustomColor_<k>). Si scrive SEMPRE, mai per ereditarieta':
                // il tempo e' un valore che scorre e che il globale puo'
                // congelare, quindi una parte che copiasse timeTex si
                // congelerebbe insieme a lui e non ripartirebbe piu' da sola.
                // Vedi la nota su timeTex in MeshPart.
                partUbo.dummyZero.setX(kClockOrigin + mp.timeTex);
            }

            // MODALITA' PER-PARTE. Lo shader decide dal solo ubuf.u_renderMode
            // se disegnare a colore piatto (wireframe/bordi) o illuminato:
            // senza questa riga una mesh in wireframe con globale Phong veniva
            // illuminata, e una mesh solida con globale wireframe usciva piatta.
            partUbo.renderMode = effectivePartRenderMode(o, mp);

            resourceUpdates->updateDynamicBuffer(o.ubo, k * o.uboBlockStride,
                                                 sizeof(UboData), &partUbo);
        }
    }

    // BORDO: un blocco per parte, dopo quelli delle parti (indice
    // uboPartSlots + k). Del blocco della parte servono il dominio e
    // u_meshIndex -- gli script scelgono la geometria leggendo `mesh` -- il
    // resto e' del bordo: il suo colore, opaco, senza texture, e la sua
    // modalita' (Base, Phong o Wireframe), indipendente dalla superficie.
    if (borderOn) {
        for (int k = 0; k < uboPartSlots; ++k) {
            UboData borderUbo = m_uboData;
            if (k < uboPartCount) {
                const MeshPart &mp = uboParts[k];
                borderUbo.u_min = mp.uMin;
                borderUbo.u_max = mp.uMax;
                borderUbo.v_min = mp.vMin;
                borderUbo.v_max = mp.vMax;
                borderUbo.u_meshIndex = (float)mp.meshIndex;
                applyTubeFields(borderUbo, &mp, mp.meshIndex);
            }
            borderUbo.u_isBorder = 1;
            borderUbo.color = QVector3D(o.borderColor.redF(), o.borderColor.greenF(),
                                        o.borderColor.blueF());
            borderUbo.alpha = 1.0f;
            // TEXTURE DEL BORDO: colore base a bianco (il fragment compone
            // color * texture), i suoi due colori, la sua inquadratura, il suo
            // orologio e la sua immagine, se ne ha una (altrimenti campiona
            // quella della superficie, come una fascia senza immagine propria).
            const bool borderTex = borderTextureShown(o);
            borderUbo.useTexture = borderTex ? 1 : 0;
            if (borderTex) borderUbo.color = QVector3D(1.0f, 1.0f, 1.0f);
            borderUbo.col1 = QVector3D(o.borderTexCol1.redF(), o.borderTexCol1.greenF(), o.borderTexCol1.blueF());
            borderUbo.col2 = QVector3D(o.borderTexCol2.redF(), o.borderTexCol2.greenF(), o.borderTexCol2.blueF());
            borderUbo.zoom = o.borderTexZoom;
            borderUbo.center = o.borderTexPan;
            borderUbo.rotation = o.borderTexRotation;
            borderUbo.dummyZero.setX(kClockOrigin + o.borderTimeTex);
            if (borderBindings(o) != (o.bindingsDyn ? o.bindingsDyn : o.bindings))
                borderUbo.u_noImage = 0;
            // Base / Phong / Wireframe del BORDO, non della superficie: in
            // wireframe il fragment esce col colore pieno, in Phong accende la
            // speculare (che altrove e' un flag unico della figura).
            borderUbo.renderMode = o.borderRenderMode;
            borderUbo.useSpecular = (o.borderRenderMode == 1) ? 1 : 0;
            resourceUpdates->updateDynamicBuffer(o.ubo, (uboPartSlots + k) * o.uboBlockStride,
                                                 sizeof(UboData), &borderUbo);
        }
    }
}

void GLWidget::prepareObjectTexture(SurfaceObject &o, QRhiResourceUpdateBatch *resourceUpdates)
{
    // =========================================================================
    // SCARICO TEXTURE (Superficie) — distruzione RHI nel contesto del frame.
    // Eseguito PRIMA dell'upload: se nello stesso giro arriva una nuova immagine,
    // il blocco di upload sotto ricrea comunque surfaceTexture.
    // =========================================================================
    if (o.surfaceTextureNeedsClear) {
        if (o.surfaceTexture) {
            o.surfaceTexture->destroy();
            delete o.surfaceTexture;
            o.surfaceTexture = nullptr;

            // Il binding allo slot 1 puntava a surfaceTexture appena distrutta:
            // ricolleghiamolo a m_dummyTexture (come all'init), altrimenti un frame
            // disegnato senza re-upload leggerebbe memoria liberata.
            if (o.bindings && m_dummyTexture) {
                o.bindings->setBindings({
                    QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, o.ubo),
                    QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, m_dummyTexture, m_sampler),
                    peelPrevBinding()
                });
                o.bindings->create();
            }
            // Stesso problema sui binding con dynamic offset usati dalla
            // superficie: puntavano alla texture appena distrutta.
            ensureDynamicBindings(o, m_dummyTexture);
        }
        o.surfaceTextureNeedsClear = false;
    }

    // =========================================================================
    // GESTIONE COMUNE UPLOAD TEXTURE (Superficie)
    // =========================================================================
    if (o.surfaceTextureNeedsUpload && !o.pendingSurfaceImage.isNull()) {
        if (o.surfaceTexture) {
            o.surfaceTexture->destroy();
            delete o.surfaceTexture;
        }
        o.surfaceTexture = rhi()->newTexture(QRhiTexture::RGBA8, o.pendingSurfaceImage.size(), 1);
        o.surfaceTexture->create();

        QRhiTextureSubresourceUploadDescription subresDesc(o.pendingSurfaceImage.constBits(), o.pendingSurfaceImage.sizeInBytes());
        QRhiTextureUploadEntry entry(0, 0, subresDesc);
        QRhiTextureUploadDescription uploadDesc({ entry });
        resourceUpdates->uploadTexture(o.surfaceTexture, uploadDesc);

        o.bindings->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, o.ubo),
            QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, o.surfaceTexture, m_sampler),
            peelPrevBinding()
        });
        o.bindings->create();

        // La superficie disegna dai binding con dynamic offset: senza questo
        // aggiornamento la nuova texture non comparirebbe (resterebbe la dummy).
        ensureDynamicBindings(o, o.surfaceTexture);

        o.surfaceTextureNeedsUpload = false;
    }
}

void GLWidget::prepareObjectGeometry(SurfaceObject &o, QRhiResourceUpdateBatch *resourceUpdates)
{
    if (o.engineMode == ModeImplicit) {
        // RAY MARCHING: la sola pipeline (il disegno e' un quad a schermo).
        if (!o.pipelineImplicit) buildImplicitPipeline(o);
        return;
    }

    if (!o.pipelineOpaque) {
        buildPipeline(o);
    }

    if (o.meshNeedsUpdate) {
        const auto& vertices = o.engine->getVertices();
        const auto& indices = o.engine->getIndices();

        if (vertices.empty() || indices.empty()) {
            // MESH VUOTA = NIENTE DA DISEGNARE. indexCount va azzerato:
            // senza, conserva il conteggio della mesh PRECEDENTE mentre
            // VBO/IBO contengono ancora i suoi dati, e il draw piu' sotto
            // (gate `indexCount > 0`) ridisegna la superficie vecchia.
            // Finora era latente perche' ogni percorso produceva subito
            // un'altra mesh; serve per la scena vuota, che deve poter
            // disegnare NIENTE di proposito.
            o.indexCount = 0;
        } else {
            int vSize = vertices.size() * sizeof(Vertex);
            int iSize = indices.size() * sizeof(unsigned int);

            if (o.vbo->size() < vSize) {
                o.vbo->destroy(); delete o.vbo;
                o.vbo = rhi()->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, vSize * 1.5);
                o.vbo->create();
            }

            if (o.ibo->size() < iSize) {
                o.ibo->destroy(); delete o.ibo;
                o.ibo = rhi()->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::IndexBuffer, iSize * 1.5);
                o.ibo->create();
            }

            resourceUpdates->updateDynamicBuffer(o.vbo, 0, vSize, vertices.data());
            resourceUpdates->updateDynamicBuffer(o.ibo, 0, iSize, indices.data());
            o.indexCount = indices.size();
        }
        o.meshNeedsUpdate = false;
        // Il bordo segue le parti appena rigenerate.
        o.borderUploaded = false;
    }

    // Lati della mesh custom per il bordo: solo la parte usata della texture.
    if (o.borderEdgeDirty && o.borderEdgeTex && o.borderEdgeWidth > 0) {
        QRhiTextureSubresourceUploadDescription edges(
            o.borderEdgeData.data(), quint32(o.borderEdgeData.size() * sizeof(float)));
        edges.setSourceSize(QSize(o.borderEdgeWidth, kBorderEdgeRows));
        resourceUpdates->uploadTexture(o.borderEdgeTex,
                                       QRhiTextureUploadDescription({ QRhiTextureUploadEntry(0, 0, edges) }));
        o.borderEdgeDirty = false;
    }

    // BORDO: i suoi buffer, solo quando si disegna (a raggio 0 non serve
    // caricarli; li ricarica il primo frame col bordo acceso).
    if (!o.borderUploaded && o.borderRadius > 0.0f && o.borderVbo && o.borderIbo) {
        const auto &bv = o.engine->getBorderVertices();
        const auto &bi = o.engine->getBorderIndices();
        if (!bv.empty() && !bi.empty()) {
            const int vSize = int(bv.size() * sizeof(Vertex));
            const int iSize = int(bi.size() * sizeof(unsigned int));
            if (o.borderVbo->size() < vSize) {
                o.borderVbo->destroy(); delete o.borderVbo;
                o.borderVbo = rhi()->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, vSize * 1.5);
                o.borderVbo->create();
            }
            if (o.borderIbo->size() < iSize) {
                o.borderIbo->destroy(); delete o.borderIbo;
                o.borderIbo = rhi()->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::IndexBuffer, iSize * 1.5);
                o.borderIbo->create();
            }
            resourceUpdates->updateDynamicBuffer(o.borderVbo, 0, vSize, bv.data());
            resourceUpdates->updateDynamicBuffer(o.borderIbo, 0, iSize, bi.data());

            // Linee del wireframe, con la densita' corrente.
            o.engine->buildBorderWire(o.borderWireAlong, o.borderWireAround);
            const auto &bw = o.engine->getBorderWireIndices();
            const int wSize = int(bw.size() * sizeof(unsigned int));
            if (wSize > 0 && o.borderWireIbo) {
                if (o.borderWireIbo->size() < wSize) {
                    o.borderWireIbo->destroy(); delete o.borderWireIbo;
                    o.borderWireIbo = rhi()->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::IndexBuffer, wSize * 1.5);
                    o.borderWireIbo->create();
                }
                resourceUpdates->updateDynamicBuffer(o.borderWireIbo, 0, wSize, bw.data());
            }
        }
        o.borderUploaded = true;
    }

    // Stesso trattamento del solido: a indici vuoti il ramo di upload non
    // gira, quindi wireframeIndexCount/wireframeRanges resterebbero
    // quelli della mesh precedente e le sue LINEE sopravvivrebbero alla
    // scena vuota (il ramo wireframe ha un gate suo, non coperto da
    // indexCount).
    if (o.wireframeNeedsUpdate && o.wireframeIndices.empty()) {
        o.wireframeIndexCount = 0;
        o.wireframeRanges.clear();
        o.wireframeNeedsUpdate = false;
    }

    if (o.wireframeNeedsUpdate && !o.wireframeIndices.empty()) {
        int iSize = o.wireframeIndexCount * sizeof(unsigned int);
        if (o.wireframeIbo->size() < iSize) {
            o.wireframeIbo->destroy(); delete o.wireframeIbo;
            o.wireframeIbo = rhi()->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::IndexBuffer, iSize * 1.5);
            o.wireframeIbo->create();
        }
        resourceUpdates->updateDynamicBuffer(o.wireframeIbo, 0, iSize, o.wireframeIndices.data());
        o.wireframeNeedsUpdate = false;
        o.wireQuadSerial = -1;   // le strisce dipendono dagli indici
    }
    prepareWireQuads(o, resourceUpdates);
}

QRhiResourceUpdateBatch *GLWidget::runObjectCompute(SurfaceObject &o, QRhiCommandBuffer *cb,
                                                    QRhiResourceUpdateBatch *resourceUpdates)
{
    if (o.engineMode != ModeParametric) return resourceUpdates;

    // SEZIONE TRASPORTATA DEI TUBI: prima del disegno, fuori dal render
    // pass. La passata consuma gli aggiornamenti in coda (i blocchi UBO di
    // questo frame), quindi il render pass parte senza.
    if (tubeFramesActive(o)) {
        // Tubi o anelli diversi: lo stato per anello non corrisponde piu'.
        quint64 layout = o.engine->getMeshParts().size();
        for (const MeshPart &mp : o.engine->getMeshParts()) layout = layout * 1000003u + quint64(mp.numU);
        if (layout != o.tubeFrameLayout) {
            o.tubeFrameLayout = layout;
            o.tubeFrameStateReset = true;
        }
        if (o.tubeFrameStateReset) {
            static const QByteArray zeros(kTubeFrameWorkVec4 * 16, '\0');
            resourceUpdates->uploadStaticBuffer(o.tubeFrameWork, 0, quint32(zeros.size()), zeros.constData());
            o.tubeFrameStateReset = false;
        }
        cb->beginComputePass(resourceUpdates);
        resourceUpdates = nullptr;
        cb->setComputePipeline(o.tubeFramePipeline);
        const std::vector<MeshPart> &frameParts = o.engine->getMeshParts();
        const int blocks = std::max(1, (int)frameParts.size());
        for (int k = 0; k < blocks; ++k) {
            // Stesso blocco dei draw: 0 per un tubo solo, k per le copie.
            const quint32 ofsBytes = (frameParts.size() <= 1) ? 0u : quint32(k) * o.uboBlockStride;
            const QRhiCommandBuffer::DynamicOffset ofs(0, ofsBytes);
            cb->setShaderResources(o.tubeFrameSrb, 1, &ofs);
            cb->dispatch(1, 1, 1);
        }
        cb->endComputePass();
    }

    // LATI DEL BORDO COPERTI (buildSharedEdgePipeline): prima del disegno,
    // col blocco 0 per costanti, tempo e vincoli; dominio e `mesh` di ogni
    // parte dalla tabella, ricaricata solo quando cambia. Le due passate
    // girano solo quando cambia la geometria (sharedEdgeKey): le
    // rotazioni non contano, il compute confronta punti non ruotati.
    // Si decide QUI, dopo buildPipeline, e qui si accende u_sharedEdgesOn
    // nei blocchi del bordo (scritti piu' in alto con 0): la passata nasce
    // dal vertex della pipeline, che dopo un rebuildShader si rifa' solo
    // in questo punto. Decidendo coi blocchi, nel frame in cui il bordo si
    // accende valeva ancora il vertex di prima, senza bordo: passata spenta
    // e tutti i lati disegnati, finche' qualcosa non ridisegnava la scena.
    // A BORDO SPENTO la passata gira come SONDA (borderAvailability): una
    // volta per geometria, quando resta ferma per borderProbeTimer, e se
    // ne rileggono i flag per sapere se il bordo, acceso, si vedrebbe. A
    // bordo acceso gira gia' per disegnare: la sonda ne rilegge i flag.
    const bool borderOn = o.borderRadius > 0.0f;
    const bool edgePass = sharedEdgesActive(o);
    if (!edgePass && !o.isCustomMesh && o.engine && !o.engine->isTubeModeActive())
        setBorderAvailability(o, BorderAvailability::Unknown);   // compute assente o non compilato
    if (edgePass) {
        const std::vector<MeshPart> &edgeParts = o.engine->getMeshParts();
        if (!resourceUpdates) resourceUpdates = rhi()->nextResourceUpdateBatch();
        const float on = 1.0f;
        const int edgeSlots = std::max(1, (int)edgeParts.size());
        if (borderOn) {
            for (int k = 0; k < edgeSlots; ++k)
                resourceUpdates->updateDynamicBuffer(
                    o.ubo, quint32((edgeSlots + k) * o.uboBlockStride + offsetof(UboData, u_sharedEdgesOn)),
                    sizeof(float), &on);
        }
        std::vector<QVector4D> table;
        table.reserve(1 + 2 * edgeParts.size());
        table.emplace_back(float(edgeParts.size()), 0.0f, 0.0f, 0.0f);
        for (const MeshPart &mp : edgeParts) {
            table.emplace_back(mp.uMin, mp.uMax, mp.vMin, mp.vMax);
            table.emplace_back(float(mp.meshIndex), 0.0f, 0.0f, 0.0f);
        }
        if (table != o.sharedEdgeTable) {
            resourceUpdates->uploadStaticBuffer(o.sharedEdgeParts, 0, quint32(table.size() * sizeof(QVector4D)),
                                                table.data());
            o.sharedEdgeTable = table;
        }
        // Cio' da cui dipendono i punti dei lati, oltre allo shader (che
        // azzera la chiave quando si rifa'). La sonda guarda la forma senza
        // il tempo: su una superficie animata girerebbe a ogni frame.
        QByteArray shape(reinterpret_cast<const char *>(table.data()), int(table.size() * sizeof(QVector4D)));
        shape.append(reinterpret_cast<const char *>(&m_uboData.mathParams), sizeof(QVector4D));
        shape.append(reinterpret_cast<const char *>(&m_uboData.mathParams2), sizeof(QVector4D));
        shape.append(reinterpret_cast<const char *>(&m_uboData.hasExplicitW), sizeof(int));
        shape.append(reinterpret_cast<const char *>(&m_uboData.u_meshCount), sizeof(float));
        // Animata o ferma: ripartendo, la sonda riguarda piu' istanti.
        const int animating = o.surfaceAnimating ? 1 : 0;
        shape.append(reinterpret_cast<const char *>(&animating), sizeof(int));
        QByteArray key = shape;
        key.append(reinterpret_cast<const char *>(&m_uboData.time), sizeof(float));
        // La sonda segue la geometria anche a bordo acceso (un preset
        // salvato col bordo): li' rilegge i flag della passata che gira
        // comunque, cosi' spegnendo il bordo il dock sa gia' cosa fare.
        bool probe = false, watchProbe = false;
        if (shape != o.borderProbeKey) {
            if (shape != o.borderProbePending) {
                // Geometria nuova: si aspetta che si fermi.
                o.borderProbePending = shape;
                o.borderProbeArmed = false;
                o.borderProbeWatch = false;
                o.borderProbeTimer->start();
            } else if (o.borderProbeArmed && !o.borderProbeReading) {
                o.borderProbeArmed = false;
                o.borderProbeKey = shape;
                probe = true;
            }
        } else if (o.borderProbeWatch && !o.borderProbeReading
                   && o.borderProbeWatchClock.elapsed() >= kBorderProbeWatchMs) {
            // Si aspetta che la superficie animata si apra: l'istante corrente.
            o.borderProbeWatchClock.restart();
            probe = watchProbe = true;
        }
        // SU UNA SUPERFICIE ANIMATA la sonda guarda kBorderProbeInstants
        // istanti di t in un colpo solo, per sapere se si apre in qualche
        // momento (S3 Moving Plane) o resta sempre chiusa: vedi
        // finishBorderProbe. Ogni istante coi suoi cinque blocchi UBO: scritti
        // nello stesso frame negli stessi blocchi, varrebbe l'ultimo per
        // tutti. L'ultimo istante e' quello attuale: col bordo acceso restano
        // i suoi flag. La sorveglianza (watchProbe) guarda solo l'attuale.
        const int instants = (probe && !watchProbe && o.surfaceAnimating) ? kBorderProbeInstants : 1;
        const bool run = (borderOn && key != o.sharedEdgeKey) || probe;
        if (run) {
            o.sharedEdgeKey = key;
            // Cinque passate distinte, ciascuna legge cio' che la
            // precedente scrive: campioni (fase 10), sfere dei blocchi (11),
            // test dei campioni dei lati (12), pulizia dei tratti corti
            // (13), lati coperti per intero (14). La fase sta nel loro
            // blocco UBO, dopo quelli del bordo; il resto del blocco e'
            // quello globale (costanti, tempo, vincoli).
            const int count = int(edgeParts.size());
            const int work[5] = {
                count * (4 * (kSharedEdgeSamples + 1) + (kSharedEdgeGrid + 1) * (kSharedEdgeGrid + 1)),
                count * kSharedEdgeTiles,
                count * 4 * (kSharedEdgeSamples + 1),
                count * 4 * (kSharedEdgeSamples + 1),
                count * 4 };
            if (probe) {
                o.borderProbeMeshes.clear();
                for (const MeshPart &mp : edgeParts) o.borderProbeMeshes.push_back(mp.meshIndex);
                o.borderProbeReadSerial = o.borderProbeSerial;
                o.borderProbeReading = true;
                o.borderProbeInstantsRead = instants;
                o.borderProbeArrived = 0;
            }
            for (int i = 0; i < instants; ++i) {
                for (int phase = 0; phase < 5; ++phase) {
                    UboData block = m_uboData;
                    block.u_isBorder = 10 + phase;
                    block.time = m_uboData.time + float(instants - 1 - i) * kBorderProbeStep;
                    const quint32 offset = quint32(2 * edgeSlots + 5 * i + phase) * o.uboBlockStride;
                    if (!resourceUpdates) resourceUpdates = rhi()->nextResourceUpdateBatch();
                    resourceUpdates->updateDynamicBuffer(o.ubo, offset, sizeof(UboData), &block);
                }
            }
            for (int i = 0; i < instants; ++i) {
                for (int phase = 0; phase < 5; ++phase) {
                    cb->beginComputePass(resourceUpdates);
                    resourceUpdates = nullptr;
                    cb->setComputePipeline(o.sharedEdgePipeline);
                    const QRhiCommandBuffer::DynamicOffset ofs(
                        0, quint32(2 * edgeSlots + 5 * i + phase) * o.uboBlockStride);
                    cb->setShaderResources(o.sharedEdgeSrb, 1, &ofs);
                    cb->dispatch((work[phase] + 63) / 64, 1, 1);
                    cb->endComputePass();
                }
                if (probe) {
                    // I flag di QUESTO istante; il risultato arriva dentro un
                    // frame successivo e si legge fuori da render, in coda.
                    QRhiResourceUpdateBatch *rb = rhi()->nextResourceUpdateBatch();
                    o.borderProbeResults[i] = {};
                    // La lettura arriva dopo, in coda: l'oggetto e' quello
                    // di QUESTO frame, non quello selezionato quando arriva.
                    o.borderProbeResults[i].completed = [this, obj = &o] {
                        QMetaObject::invokeMethod(this, [this, obj] { finishBorderProbe(*obj); }, Qt::QueuedConnection);
                    };
                    rb->readBackTexture(QRhiReadbackDescription(o.sharedEdgeTex), &o.borderProbeResults[i]);
                    cb->resourceUpdate(rb);
                }
            }
        }
        if (o.sharedEdgeReadbackRequested) {
            o.sharedEdgeReadbackRequested = false;
            QRhiResourceUpdateBatch *rb = rhi()->nextResourceUpdateBatch();
            o.sharedEdgeFlagsResult = {};
            o.sharedEdgeFlagsResult.completed = [obj = &o] { ++obj->sharedEdgeReadbacks; };
            rb->readBackTexture(QRhiReadbackDescription(o.sharedEdgeTex), &o.sharedEdgeFlagsResult);
            o.sharedEdgeSamplesResult = {};
            o.sharedEdgeSamplesResult.completed = [obj = &o] { ++obj->sharedEdgeReadbacks; };
            rb->readBackBuffer(o.sharedEdgeSamples, 0,
                               quint32(edgeParts.size() * kSharedEdgePartVec4 * sizeof(QVector4D)),
                               &o.sharedEdgeSamplesResult);
            cb->resourceUpdate(rb);
        }
    }
    // La lettura della sonda si chiude in un frame successivo: finche' non
    // arriva, se ne chiedono altri (una scena ferma non ne disegnerebbe).
    if (o.borderProbeReading) update();
    return resourceUpdates;
}

void GLWidget::objectDepthExtent(SurfaceObject &o, float &front, float &back) const
{
    QVector3D c;
    float r = 0.0f;
    if (o.engineMode == ModeImplicit) {
        // L'ingombro misurato sul campo; senza (script GLSL, non valutabile sulla CPU)
        // una sfera unitaria.
        if (o.boundKnown) { c = o.boundCenter; r = o.boundRadius; }
        else r = 1.0f;
    } else {
        // Una superficie parametrica si valuta sulla GPU (i vertici della CPU sono la
        // griglia (u, v)): nessuna misura dell'ingombro, si assume la sfera unitaria
        // attorno all'origine dell'oggetto -- giusta per le forme normalizzate, un
        // ordine ragionevole per le altre.
        r = 1.0f;
    }
    const float zc = (m_view * o.model).map(c).z();
    const float scale = (o.model * QVector4D(1.0f, 0.0f, 0.0f, 0.0f)).toVector3D().length();
    front = zc + r * scale;
    back = zc - r * scale;
}

// L'oggetto ha qualcosa da disegnare nella fase dei trasparenti.
bool GLWidget::hasTransparentPart(const SurfaceObject &o) const
{
    if (o.engineMode == ModeImplicit)
        return (o.implicitIllConditioned ? 1.0f : o.alpha) < 0.999f;
    if (!o.engine) return false;
    const std::vector<MeshPart> &parts = o.engine->getMeshParts();
    if (parts.size() > 1 && !o.meshAppearanceUniform) {
        for (const MeshPart &p : parts)
            if (p.alpha >= 0.0f && p.alpha < 0.99f) return true;
    }
    return o.renderMode != 2 && o.alpha < 0.99f;
}

void GLWidget::drawObject(SurfaceObject &o, QRhiCommandBuffer *cb, const QSize &outputSize, DrawPhase phase)
{
    if (o.engineMode == ModeImplicit) {
        // Opaco fra gli opachi; trasparente dopo tutti gli opachi, che cosi' si
        // vedono attraverso. Il trasparente NON scrive la profondita' (variante
        // della pipeline, implicitPipelineFor): ordinato con gli altri
        // trasparenti per profondita', non nasconde quelli che gli stanno
        // dietro. Con un oggetto solo l'immagine e' la stessa nelle due fasi.
        const bool transparent = (o.implicitIllConditioned ? 1.0f : o.alpha) < 0.999f;
        const DrawPhase rmPhase = transparent ? DrawPhase::Transparent : DrawPhase::Opaque;
        // Le due passate dei trasparenti: indietro = il "resto" del disegno
        // diviso, avanti = la riflessione della prima faccia.
        if (transparent && (phase == DrawPhase::TransparentBack || phase == DrawPhase::TransparentFront)) {
            if (o.pipelineImplicit && m_bgVbo) {
                QRhiGraphicsPipeline *front = nullptr, *rest = nullptr;
                QRhiGraphicsPipeline *pipeline = nullptr;
                if (implicitSplitPipelines(o, front, rest))
                    pipeline = (phase == DrawPhase::TransparentBack) ? rest : front;
                else if (phase == DrawPhase::TransparentBack)
                    pipeline = implicitPipelineFor(o, true);   // senza divisione: tutto indietro
                if (pipeline) {
                    const QRhiCommandBuffer::VertexInput vbufBinding(m_bgVbo, 0);
                    cb->setGraphicsPipeline(pipeline);
                    cb->setViewport(QRhiViewport(0, 0, outputSize.width(), outputSize.height()));
                    cb->setShaderResources(o.bindings);
                    cb->setVertexInput(0, 1, &vbufBinding);
                    cb->draw(6);
                }
            }
            return;
        }
        // Depth peeling: un frammento per pixel (la sua prima faccia) nello strato.
        if (phase == DrawPhase::Peel) {
            if (transparent && o.pipelinePeel && o.pipelineImplicit && m_bgVbo) {
                const QRhiCommandBuffer::VertexInput vbufBinding(m_bgVbo, 0);
                cb->setGraphicsPipeline(o.pipelinePeel);
                cb->setViewport(QRhiViewport(0, 0, outputSize.width(), outputSize.height()));
                cb->setShaderResources(o.bindings);
                cb->setVertexInput(0, 1, &vbufBinding);
                cb->draw(6);
            }
            return;
        }
        if (phase == rmPhase && o.pipelineImplicit && m_bgVbo) {
            // Trasparente con OPACHI in scena: due parti (vedi
            // implicitSplitPipelines), la seconda -- il fondo -- per prima, che
            // si miscela da dietro in avanti. Senza opachi (o se le pipeline
            // non si fanno) il disegno e' uno solo, com'era.
            QRhiGraphicsPipeline *splitFront = nullptr, *splitRest = nullptr;
            const bool split = transparent && sceneHasOpaqueBesides(o)
                               && implicitSplitPipelines(o, splitFront, splitRest);
            const QRhiCommandBuffer::VertexInput vbufBinding(m_bgVbo, 0);
            auto drawQuad = [&](QRhiGraphicsPipeline *pipeline) {
                cb->setGraphicsPipeline(pipeline);
                cb->setViewport(QRhiViewport(0, 0, outputSize.width(), outputSize.height()));
                cb->setShaderResources(o.bindings);
                cb->setVertexInput(0, 1, &vbufBinding);
                cb->draw(6);
            };
            if (split) {
                drawQuad(splitRest);
                drawQuad(splitFront);
            } else {
                drawQuad(implicitPipelineFor(o, transparent));
            }
        }
        return;
    }

    // MULTI-MESH: una draw call per parte, ognuna col proprio blocco UBO
    // (dominio + u_meshIndex) selezionato via dynamic offset. Con una
    // parte sola il loop fa un solo giro sul blocco 0: identico a prima.
    QRhiShaderResourceBindings *srb = o.bindingsDyn ? o.bindingsDyn : o.bindings;
    const std::vector<MeshPart> &parts = o.engine->getMeshParts();

    // Una parte e' in wireframe se la sua modalita' EFFICACE e' 2.
    // Con parti che ereditano tutte, questo coincide col vecchio test
    // globale renderMode == 2.
    auto partIsWireframe = [&](const MeshPart &p) {
        return effectivePartRenderMode(o, p) == 2;
    };
    bool anyWireframe = (o.renderMode == 2);
    bool anySolid     = (o.renderMode != 2);
    if (parts.size() > 1) {
        anyWireframe = false;
        anySolid     = false;
        for (const MeshPart &p : parts) {
            if (partIsWireframe(p)) anyWireframe = true;
            else                    anySolid = true;
        }
    }

    // BORDO, per primo: e' opaco e scrive la profondita', quindi le
    // parti trasparenti disegnate dopo lo lasciano vedere attraverso e
    // il wireframe ne resta occluso dove gli passa dietro. Ogni parte
    // col suo blocco di bordo (uboPartSlots + indice della parte).
    if (phase == DrawPhase::Opaque && o.borderRadius > 0.0f && o.borderRenderMode != 2 && o.indexCount > 0
        && o.pipelineOpaque && o.borderVbo && o.borderIbo && o.borderUploaded) {
        const int partSlots = std::max(1, (int)parts.size());
        // La pipeline del bordo, col discard dei tratti coperti; senza,
        // quella della superficie (bordo intero).
        cb->setGraphicsPipeline(o.borderPipelineOpaque ? o.borderPipelineOpaque : o.pipelineOpaque);
        cb->setViewport(QRhiViewport(0, 0, outputSize.width(), outputSize.height()));
        const QRhiCommandBuffer::VertexInput borderBinding(o.borderVbo, 0);
        // Con un'immagine propria lo slot 1 e' la SUA texture.
        QRhiShaderResourceBindings *borderSrb = borderBindings(o);
        for (const SurfaceEngine::BorderRange &r : o.engine->getBorderRanges()) {
            if (r.indexCount <= 0) continue;
            const int slot = partSlots + std::clamp(r.meshIndex, 0, partSlots - 1);
            const QRhiCommandBuffer::DynamicOffset ofs(0, slot * o.uboBlockStride);
            cb->setShaderResources(borderSrb, 1, &ofs);
            cb->setVertexInput(0, 1, &borderBinding, o.borderIbo,
                               r.indexOffset * sizeof(unsigned int),
                               QRhiCommandBuffer::IndexUInt32);
            cb->drawIndexed(r.indexCount, 1, 0, r.vertexOffset);
        }
    }

    // ORDINE: prima il SOLIDO, poi il WIREFRAME.
    // La pipeline wireframe ha depthWrite = true (le linee devono
    // occludersi fra loro), quindi disegnandola per PRIMA riempiva il
    // depth buffer e le mesh solide TRASPARENTI disegnate dopo venivano
    // scartate dal depth test: a mesh wireframe presente, una mesh
    // trasparente dietro spariva del tutto. Il solido opaco scrive
    // comunque depth, quindi il wireframe disegnato dopo resta
    // correttamente occluso da cio' che gli sta davanti.
    if (anySolid && phase != DrawPhase::Lines) {
        // Solido
        if (o.indexCount > 0 && o.vbo && o.ibo) {
            cb->setViewport(QRhiViewport(0, 0, outputSize.width(), outputSize.height()));
            const QRhiCommandBuffer::VertexInput vbufBinding(o.vbo, 0);

            // Disegna una parte con la pipeline corrente. Gli indici sono
            // LOCALI alla parte, quindi il rebase sui vertici passa da
            // vertexOffset di drawIndexed; l'offset nell'index buffer e'
            // in byte.
            auto drawPart = [&](const MeshPart &p, quint32 ubOffset) {
                if (p.indexCount <= 0) return;
                const QRhiCommandBuffer::DynamicOffset ofs(0, ubOffset);
                // I binding della parte: con un'immagine propria lo
                // slot 1 e' la SUA texture.
                cb->setShaderResources(bindingsForPart(o, p), 1, &ofs);
                cb->setVertexInput(0, 1, &vbufBinding, o.ibo,
                                   p.indexOffset * sizeof(unsigned int),
                                   QRhiCommandBuffer::IndexUInt32);
                cb->drawIndexed(p.indexCount, 1, 0, p.vertexOffset);
            };

            // Mesh singola (o mesh custom senza parti): percorso storico,
            // un solo drawIndexed su tutto il buffer col blocco UBO 0.
            auto drawWhole = [&]() {
                const QRhiCommandBuffer::DynamicOffset ofs(0, 0);
                cb->setShaderResources(o.bindingsDyn ? o.bindingsDyn : o.bindings, 1, &ofs);
                cb->setVertexInput(0, 1, &vbufBinding, o.ibo, 0, QRhiCommandBuffer::IndexUInt32);
                cb->drawIndexed(o.indexCount);
            };

            const bool multi = (parts.size() > 1);

            // Alpha EFFICACE di una parte: la sua, se dichiarata,
            // altrimenti quella globale. Decide se va nel gruppo
            // trasparente o in quello opaco.
            // In ambito "All" l'alpha per-parte e' sospeso: vale il
            // globale per tutte, o una mesh con alpha proprio finirebbe
            // nel gruppo sbagliato e la superficie non si comporterebbe
            // come una sola.
            auto partAlpha = [&](const MeshPart &p) {
                if (o.meshAppearanceUniform) return o.alpha;
                return (p.alpha >= 0.0f) ? p.alpha : o.alpha;
            };
            if (multi) {
                // Le parti in wireframe sono gia' state disegnate dal
                // ramo sopra: qui vanno saltate, o comparirebbero anche
                // come solido (doppio disegno).
                std::vector<const MeshPart*> solidParts;
                solidParts.reserve(parts.size());
                for (const MeshPart &p : parts)
                    if (!partIsWireframe(p)) solidParts.push_back(&p);

                // ORDINE: prima tutte le parti OPACHE (scrivono depth),
                // poi le trasparenti; e fra queste TUTTE le facce
                // posteriori prima di QUALSIASI faccia anteriore. Fare
                // back+front parte per parte fonderebbe la parte k+1
                // sopra il fronte della parte k, invertendo la
                // profondita' fra i rami (il fronte non scrive depth).
                if (phase == DrawPhase::Opaque && o.pipelineOpaque) {
                    cb->setGraphicsPipeline(o.pipelineOpaque);
                    for (const MeshPart *p : solidParts)
                        if (partAlpha(*p) >= 0.99f)
                            drawPart(*p, p->meshIndex * o.uboBlockStride);
                }
                // TRASPARENTI ORDINATE PER PROFONDITA'.
                // Il blending non e' commutativo e questo ramo non scrive
                // depth: disegnandole nell'ordine delle PARTI vinceva
                // l'ultima anche se stava dietro, e con tori annidati uno
                // interno sembrava coprire quello esterno.
                // La distanza si stima sul CENTROIDE dei vertici della
                // parte, portato in spazio vista: e' un'approssimazione
                // (due superfici compenetranti non hanno un ordine
                // corretto) ma copre il caso dei rami annidati o
                // affiancati, che e' quello dei preset multi-mesh.
                std::vector<const MeshPart*> transp;
                transp.reserve(solidParts.size());
                for (const MeshPart *p : solidParts)
                    if (partAlpha(*p) < 0.99f) transp.push_back(p);

                const bool tBack  = (phase == DrawPhase::Transparent || phase == DrawPhase::TransparentBack);
                const bool tFront = (phase == DrawPhase::Transparent || phase == DrawPhase::TransparentFront);
                if ((tBack || tFront) && transp.size() > 1) {
                    const std::vector<Vertex> &verts = o.engine->getVertices();
                    const QMatrix4x4 mvNow = m_view * o.model;
                    auto depthOf = [&](const MeshPart *p) -> float {
                        if (p->vertexCount <= 0) return 0.0f;
                        QVector3D c(0.0f, 0.0f, 0.0f);
                        const int step = std::max(1, p->vertexCount / 64);
                        int n = 0;
                        for (int i = 0; i < p->vertexCount; i += step) {
                            const int idx = p->vertexOffset + i;
                            if (idx < 0 || idx >= (int)verts.size()) continue;
                            c += verts[idx].position.toVector3D();
                            ++n;
                        }
                        if (n == 0) return 0.0f;
                        c /= float(n);
                        // La camera guarda lungo -z: piu' NEGATIVO = piu' lontano.
                        return mvNow.map(c).z();
                    };
                    std::stable_sort(transp.begin(), transp.end(),
                                     [&](const MeshPart *a, const MeshPart *b) {
                                         return depthOf(a) < depthOf(b);
                                     });
                }

                // Depth peeling: tutte le parti trasparenti in un colpo, l'ordine
                // lo fa il depth test dello strato.
                if (phase == DrawPhase::Peel && o.pipelinePeel) {
                    cb->setGraphicsPipeline(o.pipelinePeel);
                    for (const MeshPart *p : transp)
                        drawPart(*p, p->meshIndex * o.uboBlockStride);
                }

                // Disegna solo se entrambe le pipeline di trasparenza sono
                // valide: dopo un errore di compilazione una puo' essere
                // nulla, e passarla a Metal causa EXC_BAD_ACCESS.
                if ((tBack || tFront) && o.pipelineTranspBack && o.pipelineTranspFront) {
                    if (tBack) {
                        cb->setGraphicsPipeline(o.pipelineTranspBack);
                        for (const MeshPart *p : transp)
                            drawPart(*p, p->meshIndex * o.uboBlockStride);
                    }
                    if (tFront) {
                        cb->setGraphicsPipeline(o.pipelineTranspFront);
                        for (const MeshPart *p : transp)
                            drawPart(*p, p->meshIndex * o.uboBlockStride);
                    }
                }
            } else if (o.alpha < 0.99f) {
                if (phase == DrawPhase::Peel && o.pipelinePeel) {
                    cb->setGraphicsPipeline(o.pipelinePeel);
                    drawWhole();
                }
                const bool tBack  = (phase == DrawPhase::Transparent || phase == DrawPhase::TransparentBack);
                const bool tFront = (phase == DrawPhase::Transparent || phase == DrawPhase::TransparentFront);
                if ((tBack || tFront) && o.pipelineTranspBack && o.pipelineTranspFront) {
                    if (tBack) {
                        cb->setGraphicsPipeline(o.pipelineTranspBack);
                        drawWhole();
                    }
                    if (tFront) {
                        cb->setGraphicsPipeline(o.pipelineTranspFront);
                        drawWhole();
                    }
                }
            } else {
                if (phase == DrawPhase::Opaque && o.pipelineOpaque) {
                    cb->setGraphicsPipeline(o.pipelineOpaque);
                    drawWhole();
                }
            }
        }
    }

    // WIREFRAME, dopo il solido (vedi la nota sull'ordine sopra).
    if (anyWireframe && phase == DrawPhase::Lines) {
        if (m_captureLineWidth > 1 && o.wireQuadPipeline && o.wireQuadVbo && o.wireQuadSerial == m_captureSerial
            && o.wireframeIndexCount > 0) {
            // Cattura di un'immagine: strisce di triangoli, una istanza per segmento.
            cb->setGraphicsPipeline(o.wireQuadPipeline);
            cb->setViewport(QRhiViewport(0, 0, outputSize.width(), outputSize.height()));
            auto drawSegments = [&](int firstSegment, int segments, int meshIndex) {
                const QRhiCommandBuffer::DynamicOffset ofs(0, meshIndex * o.uboBlockStride);
                cb->setShaderResources(srb, 1, &ofs);
                const QRhiCommandBuffer::VertexInput inst(o.wireQuadVbo, quint32(firstSegment) * 8 * sizeof(float));
                cb->setVertexInput(0, 1, &inst);
                cb->draw(6, segments);
            };
            if (o.wireframeRanges.size() <= 1) {
                drawSegments(0, o.wireframeIndexCount / 2, 0);
            } else {
                for (const WireframeRange &r : o.wireframeRanges) {
                    if (r.indexCount <= 0) continue;
                    if (r.meshIndex >= 0 && r.meshIndex < (int)parts.size()
                        && !partIsWireframe(parts[r.meshIndex])) continue;
                    drawSegments(r.indexOffset / 2, r.indexCount / 2, r.meshIndex);
                }
            }
        } else if (o.wireframePipeline && o.wireframeIndexCount > 0) {
            cb->setGraphicsPipeline(o.wireframePipeline);
            const QRhiCommandBuffer::VertexInput vbufBinding(o.vbo, 0);

            // Con uno spessore > 1 (cattura di immagini) il wireframe si ridisegna con
            // la finestra spostata: vedi setCaptureLineWidth.
            for (const QPoint &off : wireframeOffsets()) {
            cb->setViewport(QRhiViewport(off.x(), off.y(), outputSize.width(), outputSize.height()));
            if (o.wireframeRanges.size() <= 1) {
                const QRhiCommandBuffer::DynamicOffset ofs(0, 0);
                cb->setShaderResources(srb, 1, &ofs);
                cb->setVertexInput(0, 1, &vbufBinding, o.wireframeIbo, 0, QRhiCommandBuffer::IndexUInt32);
                cb->drawIndexed(o.wireframeIndexCount);
            } else {
                for (const WireframeRange &r : o.wireframeRanges) {
                    if (r.indexCount <= 0) continue;
                    // Salta le parti che NON sono in wireframe: i loro
                    // indici esistono comunque nel buffer (buildWireframe
                    // genera un range per ogni parte), ma vanno disegnate
                    // dal ramo solido.
                    if (r.meshIndex >= 0 && r.meshIndex < (int)parts.size()
                        && !partIsWireframe(parts[r.meshIndex])) continue;
                    const QRhiCommandBuffer::DynamicOffset ofs(0, r.meshIndex * o.uboBlockStride);
                    cb->setShaderResources(srb, 1, &ofs);
                    // Gli indici del wireframe sono ASSOLUTI: l'offset
                    // della parte si applica all'index buffer (in byte),
                    // non ai vertici.
                    cb->setVertexInput(0, 1, &vbufBinding, o.wireframeIbo,
                                       r.indexOffset * sizeof(unsigned int),
                                       QRhiCommandBuffer::IndexUInt32);
                    cb->drawIndexed(r.indexCount);
                }
            }
            }
        }
    }

    // BORDO IN WIREFRAME: con la pipeline a linee, dopo il solido per
    // la stessa ragione del wireframe della superficie (scrive la
    // profondita' e non deve nascondere le parti trasparenti).
    if (phase == DrawPhase::Lines && o.borderRadius > 0.0f && o.borderRenderMode == 2 && o.indexCount > 0
        && o.wireframePipeline && o.borderVbo && o.borderWireIbo && o.borderUploaded) {
        const int partSlots = std::max(1, (int)parts.size());
        cb->setGraphicsPipeline(o.borderPipelineWire ? o.borderPipelineWire : o.wireframePipeline);
        const QRhiCommandBuffer::VertexInput borderBinding(o.borderVbo, 0);
        for (const QPoint &off : wireframeOffsets()) {
        cb->setViewport(QRhiViewport(off.x(), off.y(), outputSize.width(), outputSize.height()));
        for (const SurfaceEngine::BorderRange &r : o.engine->getBorderRanges()) {
            if (r.wireIndexCount <= 0) continue;
            const int slot = partSlots + std::clamp(r.meshIndex, 0, partSlots - 1);
            const QRhiCommandBuffer::DynamicOffset ofs(0, slot * o.uboBlockStride);
            cb->setShaderResources(srb, 1, &ofs);
            cb->setVertexInput(0, 1, &borderBinding, o.borderWireIbo,
                               r.wireIndexOffset * sizeof(unsigned int),
                               QRhiCommandBuffer::IndexUInt32);
            cb->drawIndexed(r.wireIndexCount, 1, 0, r.vertexOffset);
        }
        }
    }
}

void GLWidget::drawFlatObject(SurfaceObject &o, QRhiCommandBuffer *cb, const QSize &outputSize)
{
    // Ray Marching: in vista 2D solo lo sfondo (disegnato da render()).
    if (o.engineMode != ModeParametric) return;
    if (m_flatViewTarget == 2 && o.borderRadius > 0.0f && o.pipelineOpaque && m_bgVbo) {
        // TEXTURE DEL BORDO in 2D: lo stesso quadrato, col blocco di
        // uniform del bordo (u_isBorder: il fragment sceglie la sua
        // texture; colori, inquadratura e orologio suoi) e la sua
        // immagine. Il blocco e' quello del bordo della prima parte.
        cb->setGraphicsPipeline(o.pipelineOpaque);
        cb->setViewport(QRhiViewport(0, 0, outputSize.width(), outputSize.height()));
        const int partSlots = std::max(1, (int)o.engine->getMeshParts().size());
        const QRhiCommandBuffer::DynamicOffset dynOfs0(0, partSlots * o.uboBlockStride);
        cb->setShaderResources(borderBindings(o), 1, &dynOfs0);
        const QRhiCommandBuffer::VertexInput vbufBinding(m_bgVbo, 0);
        cb->setVertexInput(0, 1, &vbufBinding);
        cb->draw(6);
    } else if (o.pipelineOpaque && m_bgVbo) {
        cb->setGraphicsPipeline(o.pipelineOpaque);
        cb->setViewport(QRhiViewport(0, 0, outputSize.width(), outputSize.height()));
        // pipelineOpaque e' costruita sul layout con dynamic offset:
        // anche questo draw (quad di anteprima, nessuna parte di mesh)
        // deve passare un offset.
        // MULTI-MESH: la vista 2D edita la texture della mesh SELEZIONATA,
        // quindi deve leggere il blocco UBO di QUELLA parte. Col blocco 0
        // fisso, u_meshIndex valeva sempre 0 e il dispatcher mostrava la
        // texture della mesh 1 mentre il dock script mostrava quella
        // selezionata: stesso indice, due texture diverse a schermo.
        // L'indice va CLAMPATO agli stessi limiti che usa
        // commitFlatTransformToActivePart, o si disegna un blocco mentre
        // il mouse ne scrive un altro e il trascinamento sembra non
        // funzionare (il "a tratti" storico: capita quando l'indice
        // attivo sopravvive a una rigenerazione con meno parti).
        // Nota: con uboPartCount <= 1 e' scritto SOLO il blocco 0, quindi
        // qualunque altro offset leggerebbe un blocco mai aggiornato.
        const std::vector<MeshPart> &flatParts = o.engine->getMeshParts();
        const int flatPart = (flatParts.size() > 1 && o.activeMeshPart >= 0
                              && o.activeMeshPart < (int)flatParts.size())
                             ? o.activeMeshPart : 0;
        const QRhiCommandBuffer::DynamicOffset dynOfs0(0, flatPart * o.uboBlockStride);
        // ...e con la SUA immagine, se ne ha una.
        QRhiShaderResourceBindings *flatSrb = o.bindingsDyn ? o.bindingsDyn : o.bindings;
        if (flatParts.size() > 1 && o.activeMeshPart >= 0
            && o.activeMeshPart < (int)flatParts.size())
            flatSrb = bindingsForPart(o, flatParts[flatPart]);
        cb->setShaderResources(flatSrb, 1, &dynOfs0);
        const QRhiCommandBuffer::VertexInput vbufBinding(m_bgVbo, 0);
        cb->setVertexInput(0, 1, &vbufBinding);
        cb->draw(6);
    }
}

// ==========================================================
// DEPTH PEELING DEI TRASPARENTI (vedi glwidget.h, m_peel)
// ==========================================================

QRhiShaderResourceBinding GLWidget::peelPrevBinding() const
{
    return QRhiShaderResourceBinding::sampledTexture(
        5, QRhiShaderResourceBinding::FragmentStage,
        m_peelPrev ? m_peelPrev : m_dummyTexture,
        m_peelSampler ? m_peelSampler : m_sampler);
}

// I binding di superficie puntano a m_peelPrev: ricreata quella, vanno riagganciati
// (stessa tecnica di ensureUboCapacity: updateResources, mai delete+new, le
// pipeline ne tengono il puntatore).
void GLWidget::refreshSurfaceBindings()
{
    for (const auto &op : m_objects) {
        SurfaceObject &o = *op;
        if (o.bindingsDyn) {
            QRhiTexture *tex = o.bindingsDynTexture;
            o.bindingsDynTexture = nullptr;
            ensureDynamicBindings(o, tex);
        }
        rebindPartImages(o);
        // Anche i binding statici (ray marching): stesso layout, risorsa nuova.
        if (o.bindings && o.ubo) {
            o.bindings->setBindings({
                QRhiShaderResourceBinding::uniformBuffer(
                    0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, o.ubo),
                QRhiShaderResourceBinding::sampledTexture(
                    1, QRhiShaderResourceBinding::FragmentStage,
                    o.surfaceTexture ? o.surfaceTexture : m_dummyTexture, m_sampler),
                peelPrevBinding()
            });
            o.bindings->updateResources();
        }
    }
}

void GLWidget::releasePeelResources(bool all)
{
    for (PeelLayer &l : m_peel) {
        delete l.compSrb;
        delete l.rt;
        if (l.color) { l.color->destroy(); delete l.color; }
        if (l.depth) { l.depth->destroy(); delete l.depth; }
        l = PeelLayer();
    }
    m_peelLayers = 0;
    m_peelSize = QSize();
    delete m_peelPrevRt;
    m_peelPrevRt = nullptr;
    if (m_peelPrev) { m_peelPrev->destroy(); delete m_peelPrev; m_peelPrev = nullptr; }
    delete m_peelDepthRb;
    m_peelDepthRb = nullptr;
    if (!all) return;
    delete m_peelPrevRp;
    m_peelPrevRp = nullptr;
    delete m_peelRp;
    m_peelRp = nullptr;
    delete m_peelCompositePipeline;
    m_peelCompositePipeline = nullptr;
    delete m_peelSampler;
    m_peelSampler = nullptr;
}

bool GLWidget::ensurePeelResources(const QSize &size, int layers)
{
    if (m_peelFailed || !rhi() || layers < 2 || layers > kMaxPeelLayers) return false;
    if (m_peelLayers == layers && m_peelSize == size && m_peel[0].rt && m_peelCompositePipeline) return true;

    auto fail = [this]() {
        qWarning() << "depth peeling: risorse non create, i trasparenti si disegnano a due passate.";
        releasePeelResources(true);
        m_peelFailed = true;
        refreshSurfaceBindings();
        return false;
    };
    if (!rhi()->isTextureFormatSupported(QRhiTexture::R32F, QRhiTexture::RenderTarget)) return fail();

    releasePeelResources(false);
    if (!m_peelSampler) {
        m_peelSampler = rhi()->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None,
                                          QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
        if (!m_peelSampler->create()) return fail();
    }
    m_peelDepthRb = rhi()->newRenderBuffer(QRhiRenderBuffer::DepthStencil, size, 1);
    if (!m_peelDepthRb->create()) return fail();

    for (int k = 0; k < layers; ++k) {
        PeelLayer &l = m_peel[k];
        l.color = rhi()->newTexture(QRhiTexture::RGBA8, size, 1, QRhiTexture::RenderTarget);
        l.depth = rhi()->newTexture(QRhiTexture::R32F, size, 1, QRhiTexture::RenderTarget);
        if (!l.color->create() || !l.depth->create()) return fail();
        QRhiTextureRenderTargetDescription desc;
        desc.setColorAttachments({ QRhiColorAttachment(l.color), QRhiColorAttachment(l.depth) });
        desc.setDepthStencilBuffer(m_peelDepthRb);
        l.rt = rhi()->newTextureRenderTarget(desc);
        if (!m_peelRp) m_peelRp = l.rt->newCompatibleRenderPassDescriptor();
        l.rt->setRenderPassDescriptor(m_peelRp);
        if (!l.rt->create()) return fail();
        // La composizione legge colore e profondita' dello strato per texel.
        l.compSrb = rhi()->newShaderResourceBindings();
        l.compSrb->setBindings({
            QRhiShaderResourceBinding::sampledTexture(0, QRhiShaderResourceBinding::FragmentStage, l.color, m_peelSampler),
            QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, l.depth, m_peelSampler)
        });
        if (!l.compSrb->create()) return fail();
    }

    // La profondita' dello strato PRECEDENTE, letta dallo shader degli oggetti.
    m_peelPrev = rhi()->newTexture(QRhiTexture::R32F, size, 1, QRhiTexture::RenderTarget);
    if (!m_peelPrev->create()) return fail();
    QRhiTextureRenderTargetDescription prevDesc;
    prevDesc.setColorAttachments({ QRhiColorAttachment(m_peelPrev) });
    m_peelPrevRt = rhi()->newTextureRenderTarget(prevDesc);
    if (!m_peelPrevRp) m_peelPrevRp = m_peelPrevRt->newCompatibleRenderPassDescriptor();
    m_peelPrevRt->setRenderPassDescriptor(m_peelPrevRp);
    if (!m_peelPrevRt->create()) return fail();

    if (!m_peelCompositePipeline) {
        static const char *vsSrc =
            "#version 440\n"
            "void main() {\n"
            "    vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));\n"
            "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
            "}\n";
        static const char *fsSrc =
            "#version 440\n"
            "layout(location = 0) out vec4 o;\n"
            "layout(binding = 0) uniform sampler2D uColor;\n"
            "layout(binding = 1) uniform sampler2D uDepth;\n"
            "void main() {\n"
            "    ivec2 c = ivec2(gl_FragCoord.xy);\n"
            "    vec4 col = texelFetch(uColor, c, 0);\n"
            "    if (col.a <= 0.0) discard;\n"
            "    gl_FragDepth = texelFetch(uDepth, c, 0).r;\n"
            "    o = col;\n"
            "}\n";
        const QShader vs = bakeShader(vsSrc, QShader::VertexStage);
        const QShader fs = bakeShader(fsSrc, QShader::FragmentStage);
        if (!vs.isValid() || !fs.isValid()) return fail();
        QRhiGraphicsPipeline::TargetBlend blend;
        blend.enable   = true;
        blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
        blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        blend.srcAlpha = QRhiGraphicsPipeline::One;
        blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        m_peelCompositePipeline = rhi()->newGraphicsPipeline();
        m_peelCompositePipeline->setShaderStages({ { QRhiShaderStage::Vertex, vs }, { QRhiShaderStage::Fragment, fs } });
        m_peelCompositePipeline->setSampleCount(renderTarget()->sampleCount());
        m_peelCompositePipeline->setDepthTest(true);
        m_peelCompositePipeline->setDepthWrite(false);
        m_peelCompositePipeline->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
        m_peelCompositePipeline->setCullMode(QRhiGraphicsPipeline::None);
        m_peelCompositePipeline->setTargetBlends({ blend });
        m_peelCompositePipeline->setShaderResourceBindings(m_peel[0].compSrb);
        m_peelCompositePipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
        if (!m_peelCompositePipeline->create()) return fail();
    }

    m_peelLayers = layers;
    m_peelSize = size;
    refreshSurfaceBindings();
    return true;
}

// La variante PEEL del fragment dell'oggetto: no culling (facce anteriori e
// posteriori sono strati come gli altri), profondita' scritta (il piu' vicino
// vince), niente blend (lo strato tiene un solo frammento per pixel).
bool GLWidget::ensurePeelPipeline(SurfaceObject &o)
{
    const bool implicit = (o.engineMode == ModeImplicit);
    // Chiave del sorgente: la generazione dello shader RM (negativa) o parametrico.
    const int key = implicit ? -(o.implicitPipelineGen + 1) : o.peelSourceGen;
    if (o.peelBuiltGen == key && o.pipelinePeel) return true;
    if (o.peelBuiltGen == key) return false;
    // Una volta per sorgente, riuscisse o no: un fallimento non si ritenta a ogni frame.
    o.peelBuiltGen = key;
    delete o.pipelinePeel;
    o.pipelinePeel = nullptr;
    if (implicit) {
        if (!o.pipelineImplicit || !o.implicitVs.isValid() || !m_peelRp) return false;
        const QShader fsRm = bakeShader(createImplicitFragmentShader(o, 3).toUtf8(), QShader::FragmentStage);
        if (!fsRm.isValid()) return false;
        QRhiVertexInputLayout rmLayout;
        rmLayout.setBindings({ { sizeof(Vertex) } });
        rmLayout.setAttributes({
            { 0, 0, QRhiVertexInputAttribute::Float3, offsetof(Vertex, position) },
            { 0, 1, QRhiVertexInputAttribute::Float4, offsetof(Vertex, normal) },
            { 0, 2, QRhiVertexInputAttribute::Float2, offsetof(Vertex, texCoord) }
        });
        QRhiGraphicsPipeline::TargetBlend off;
        off.enable = false;
        QRhiGraphicsPipeline *rp = rhi()->newGraphicsPipeline();
        rp->setTopology(QRhiGraphicsPipeline::Triangles);
        rp->setVertexInputLayout(rmLayout);
        rp->setShaderStages({ { QRhiShaderStage::Vertex, o.implicitVs }, { QRhiShaderStage::Fragment, fsRm } });
        rp->setSampleCount(1);
        rp->setDepthTest(true);
        rp->setDepthWrite(true);
        rp->setDepthOp(QRhiGraphicsPipeline::Less);
        rp->setCullMode(QRhiGraphicsPipeline::None);
        rp->setTargetBlends({ off, off });
        rp->setShaderResourceBindings(o.bindings);
        rp->setRenderPassDescriptor(m_peelRp);
        if (!rp->create()) {
            delete rp;
            return false;
        }
        o.pipelinePeel = rp;
        return true;
    }
    if (o.peelFsSource.isEmpty() || !o.peelVs.isValid() || !m_peelRp || !o.bindingsDyn) return false;
    const QShader fs = bakeShader(o.peelFsSource.toUtf8(), QShader::FragmentStage);
    if (!fs.isValid()) return false;

    QRhiVertexInputLayout inputLayout;
    inputLayout.setBindings({ { sizeof(Vertex) } });
    inputLayout.setAttributes({
        { 0, 0, QRhiVertexInputAttribute::Float4, offsetof(Vertex, position) },
        { 0, 1, QRhiVertexInputAttribute::Float4, offsetof(Vertex, normal)   },
        { 0, 2, QRhiVertexInputAttribute::Float2, offsetof(Vertex, texCoord) }
    });
    QRhiGraphicsPipeline::TargetBlend noBlend;
    noBlend.enable = false;
    QRhiGraphicsPipeline *p = rhi()->newGraphicsPipeline();
    p->setVertexInputLayout(inputLayout);
    p->setShaderStages({ { QRhiShaderStage::Vertex, o.peelVs }, { QRhiShaderStage::Fragment, fs } });
    p->setSampleCount(1);
    p->setDepthTest(true);
    p->setDepthWrite(true);
    p->setDepthOp(QRhiGraphicsPipeline::Less);
    p->setCullMode(QRhiGraphicsPipeline::None);
    p->setTargetBlends({ noBlend, noBlend });
    p->setShaderResourceBindings(o.bindingsDyn);
    p->setRenderPassDescriptor(m_peelRp);
    if (!p->create()) {
        qWarning() << "buildPipeline: create() peel fallita.";
        delete p;
        return false;
    }
    o.pipelinePeel = p;
    return true;
}

bool GLWidget::renderPeelLayers(QRhiCommandBuffer *cb, const QSize &outputSize, int layers,
                                const std::vector<SurfaceObject *> &objs, QRhiResourceUpdateBatch *&updates)
{
    if (!ensurePeelResources(outputSize, layers)) return false;
    for (SurfaceObject *o : objs)
        if (!ensurePeelPipeline(*o)) return false;

    // Da qui non si torna indietro: il batch dei buffer entra nella prima passata.
    // Lo strato 0 parte da profondita' 0 (nulla da scartare).
    cb->beginPass(m_peelPrevRt, QColor(0, 0, 0, 0), { 1.0f, 0 }, updates);
    updates = nullptr;
    cb->endPass();
    for (int k = 0; k < m_peelLayers; ++k) {
        cb->beginPass(m_peel[k].rt, QColor(0, 0, 0, 0), { 1.0f, 0 });
        for (SurfaceObject *o : objs) drawObject(*o, cb, outputSize, DrawPhase::Peel);
        cb->endPass();
        if (k + 1 < m_peelLayers) {
            QRhiResourceUpdateBatch *copy = rhi()->nextResourceUpdateBatch();
            copy->copyTexture(m_peelPrev, m_peel[k].depth);
            cb->resourceUpdate(copy);
        }
    }
    return true;
}

// Gli strati nel main pass, dal piu' lontano al piu' vicino. La profondita' dello
// strato fa da gl_FragDepth: gli opachi gia' disegnati coprono cio' che sta dietro.
void GLWidget::drawPeelComposite(QRhiCommandBuffer *cb, const QSize &outputSize)
{
    if (!m_peelCompositePipeline) return;
    for (int k = m_peelLayers - 1; k >= 0; --k) {
        cb->setGraphicsPipeline(m_peelCompositePipeline);
        cb->setViewport(QRhiViewport(0, 0, outputSize.width(), outputSize.height()));
        cb->setShaderResources(m_peel[k].compSrb);
        cb->draw(3);
    }
}
