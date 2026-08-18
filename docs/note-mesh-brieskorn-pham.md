# Le mesh del preset Brieskorn-Pham

Nota di lavoro sul meccanismo multi-mesh, presa a partire dal preset
`Brieskorn-Pham.json`. Descrive come funziona il motore oggi (parsing in
`mainwindow.cpp`, disegno in `glwidget.cpp`), non come dovrebbe funzionare.

---

## 1. Il quadro in una frase

Lo script **dichiara 49 mesh** con 49 blocchi `//MESH_BEGIN`, ne **disegna solo
A×B** grazie al blocco `//CUTOUT`, e **dice all'interfaccia quante sono vive**
con la riga `MESH_VISIBLE := A*B;`.

Sono tre meccanismi distinti che devono restare d'accordo fra loro. Se ne tocchi
uno solo, il preset si rompe in modi diversi a seconda di quale.

---

## 2. Cosa fa un blocco `//MESH_BEGIN`

```
//MESH_BEGIN
u: 0, pi/2, 40
v: -1, 1, 40
//MESH_END
```

Ogni blocco crea **una parte di mesh**: una griglia di vertici separata, con il
proprio dominio in u e in v. Nel preset ce ne sono 49 identiche, tutte sul
dominio `u ∈ [0, π/2]`, `v ∈ [-1, 1]`.

Due cose non ovvie:

**I blocchi portano dominio e risoluzione, non l'aspetto.** Colore, alpha, luce,
wireframe e texture di una parte si impostano dall'interfaccia (dock Renderer,
selettore Mesh), non da qui.

**Il terzo numero è una proporzione, non un conteggio.** `40` non significa "40
passi": significa che questa parte pesa 40 rispetto alle altre. La risoluzione
vera la governa lo slider **Steps** (qui a 160). La parte col valore dichiarato
più alto prende esattamente il valore dello slider, le altre restano in
proporzione. Nel preset tutte le parti dichiarano `40` su entrambi gli assi,
quindi sono tutte uguali e seguono lo slider insieme.

**Le espressioni sono valutate sulla CPU**, non nello shader: `pi/2` funziona
perché lo legge ExprTk mentre costruisce la griglia. Per lo stesso motivo qui
**non puoi usare le costanti A..S**: quando questi numeri vengono letti, gli
slider non c'entrano ancora nulla. È esattamente il motivo per cui il preset
dichiara 49 blocchi fissi invece di dichiararne A×B.

---

## 3. Come lo script sa quale mesh sta disegnando

Nel corpo dello script hai a disposizione la variabile **`mesh`**: l'indice della
parte in corso di disegno, 0 per la prima.

Tecnicamente arriva dall'uniform `u_meshIndex`, che è **costante su tutto il
disegno di una parte** (una UBO per parte, selezionata via dynamic offset). Il
traduttore la inietta da solo come `float mesh = ubuf.u_meshIndex;`.

Il preset la usa per decomporre l'indice piatto in due indici di radice:

```glsl
float k1 = mod(float(mesh), 7.0);        // 0..6
float k2 = floor(float(mesh) / 7.0);     // 0..6
```

**La base 7 è fissa e deve restare fissa.** 7 è il massimo dichiarato dagli
slider (`int(2,7)`), non il valore corrente. Se scrivessi `mod(mesh, na)` con
`na` il valore vivo dello slider, la stessa mesh cambierebbe significato al
variare di A, e le patch salterebbero da una posizione all'altra invece di
apparire e sparire. Il commento nello script lo dice esplicitamente: *"Gli indici
restano in base 7 SEMPRE"*.

Da qui il resto è matematica: ogni coppia `(k1, k2)` sceglie una radice
dell'unità per `z1` e una per `z2`, e la patch corrispondente è un pezzo della
superficie `z1^a + z2^b = 1`.

---

## 4. Come si spengono le mesh in eccesso: il `//CUTOUT`

Questa è la risposta diretta alla tua domanda — **sì, puoi disegnare solo una
parte delle mesh disponibili, e il meccanismo è il cutout.**

```glsl
//CUTOUT_BEGIN
float ca = clamp(floor(A + 0.5), 2.0, 7.0);
float cb = clamp(floor(B + 0.5), 2.0, 7.0);
return (mod(float(mesh), 7.0) > ca - 0.5) || (floor(float(mesh) / 7.0) > cb - 0.5);
//CUTOUT_END
```

Il blocco diventa una funzione GLSL `cutHere(u, v)` nel fragment shader, e dove
torna `true` il frammento viene **scartato** (`discard`). Ritorna `true` quando
`k1` sfora l'esponente A o `k2` sfora l'esponente B: con A=3, B=5 sopravvivono le
patch con `k1 ∈ {0,1,2}` e `k2 ∈ {0,...,4}`, cioè 15 delle 49.

Tre conseguenze pratiche, tutte importanti:

**Le mesh spente esistono ancora.** La geometria è stata generata, i vertici sono
in memoria, il vertex shader li ha trasformati. Il cutout agisce **per pixel**,
all'ultimo stadio. Non risparmi memoria e non risparmi lavoro sul vertex shader:
risparmi solo il riempimento. Con 49 griglie 160×160 è un costo che si sente su
GPU mobile, ed è il prezzo pagato per avere slider che accendono e spengono le
patch senza rigenerare nulla.

**Il clamp va replicato identico.** Nel corpo principale c'è
`clamp(floor(A + 0.5), 2.0, 7.0)` come `na`, nel cutout come `ca`: sono la stessa
formula scritta due volte. Il commento nel preset avverte che vanno tenute
allineate, *"o le patch vive non coincidono"* — cioè disegneresti una patch con
una geometria calcolata per un esponente e la taglieresti secondo un altro.

**Il cutout non è deducibile dalla CPU.** È GLSL che gira sulla GPU. Il C++ non
sa quali mesh sopravvivono, e rifare il conto in C++ sarebbe la solita logica
duplicata che diverge. Da qui il terzo meccanismo.

---

## 5. `MESH_VISIBLE`: limitare il contatore dell'interfaccia

```
MESH_VISIBLE := A*B;
```

Questa riga serve **solo all'interfaccia**, non al disegno, e fa esattamente la
cosa sensata: **limita il selettore di mesh alle patch che si vedono davvero.**
Con A=3, B=5 lo spinbox arriva a 15, non a 49.

Il codice è in `updateMeshSelectorRange()`: prende il conteggio dichiarato dal
motore e lo taglia col numero vivo.

```cpp
int n = ui->glWidget->meshPartCount();   // 49
const int nVis = meshVisibleCount();     // A*B = 15
if (nVis > 0) n = std::min(n, nVis);
```

Senza questa direttiva `nVis` vale 0 e resta il conteggio dichiarato — è il
comportamento di default per gli script che non la usano. Il commento nel codice
spiega perché la direttiva è necessaria: il cutout è GLSL eseguito sulla GPU, il
C++ **non può sapere** quali mesh sopravvivono, e rifare il conto in C++ sarebbe
la solita logica duplicata che diverge. Perciò è lo script a dichiararlo.

L'espressione è **rivalutata a ogni cambio di costante**, non memorizzata: deve
seguire gli slider. Le lettere A..S vengono sostituite col loro valore numerico
*prima* di dare il testo a ExprTk — perché ExprTk è case-insensitive e conosce
già `e` come numero di Nepero, che vincerebbe su una costante chiamata `E`.

**Il punto fragile.** `MESH_VISIBLE` e il cutout sono due conti separati sulla
stessa cosa, tenuti allineati a mano. Qui tornano — il cutout lascia vive le
patch con `k1 < A` e `k2 < B`, cioè A×B, e la direttiva dice `A*B` — ma niente lo
verifica. Se cambi la condizione del cutout senza aggiornare la direttiva, il
selettore mostra un numero e lo schermo un altro. È l'unico punto di questo
preset dove un errore non dà segnali.

### Cosa resta inaccessibile, e perché

Il *contatore* è limitato. Quello che resta è una cosa diversa: le 34 mesh spente
**esistono come geometria in memoria** — griglia generata, vertici caricati,
vertex shader eseguito — anche se non le puoi né vedere né selezionare.

Non è un problema di interfaccia, è il costo della scelta architetturale. I
blocchi `//MESH_BEGIN` sono letti dalla CPU **al Run**, quando gli slider non
hanno ancora un valore: per generare solo A×B griglie bisognerebbe **rigenerare
tutta la geometria a ogni scatto di A o B**. Il cutout esiste proprio per
evitarlo, spostando la decisione sulla GPU dove costa un `discard` per pixel.

Il preset quindi paga memoria e tempo di Run per avere slider istantanei. È un
compromesso ragionevole a 49 patch; diventa discutibile se il numero cresce
(vedi sotto).

## 5-bis. Il contatore e le mesh scartate (risolto nel preset)

C'era un difetto, ed è stato corretto **dentro lo script**, senza toccare il C++.

### Il problema

`MESH_VISIBLE` diceva all'interfaccia **quante** patch sopravvivono, e lo spinbox
mappava il numero sull'indice nel modo più diretto:

```cpp
setActiveMeshPart(single ? ui->spinMeshSel->value() - 1 : -1);   // numero 4 -> mesh 3
```

L'assunzione implicita è che le vive siano **le prime N in ordine**. Con gli
indici in base 7 fissa non lo erano: il cutout scartava per riga e colonna, non
per indice piatto.

```
A=3, B=5  ->  15 vive, ma agli indici 0,1,2, 7,8,9, 14,15,16, 21,...
              lo spinbox offriva 1..15  ->  mesh 0..14
```

Dal numero 4 in poi si selezionavano patch scartate: slider inerti. Su 15
selezionabili ne funzionavano 3. Il caso `A=B=7` (griglia piena) era l'unico in
cui gli indici erano contigui, ed è probabilmente il motivo per cui il difetto
era sfuggito.

### La correzione

Gli indici ora si calcolano sulla **griglia viva** invece che sulla base fissa:

```glsl
float k1 = mod(float(mesh), na);      // era: mod(float(mesh), 7.0)
float k2 = floor(float(mesh) / na);   // era: floor(float(mesh) / 7.0)
```

e il cutout, di conseguenza, taglia una **coda contigua** invece di una
sottogriglia:

```glsl
return float(mesh) > ca * cb - 0.5;   // era: due confronti su riga e colonna
```

Le patch vive diventano le prime `A*B`, quindi ogni numero offerto dallo spinbox
punta a una patch che si vede. `MESH_VISIBLE := A*B;` resta invariata e ora è
esatta anche come *mappatura*, non solo come conteggio.

**La figura non cambia.** Verificato su tutte le 36 combinazioni degli slider: le
coppie `(k1, k2)` disegnate sono identiche a prima: cambia quale indice porta
quale patch, non l'insieme delle patch. Il cutout è anche diventato più
economico, un confronto invece di due.

### Perché questa strada

Le alternative erano rinumerare lo spinbox in C++ (richiede la lista degli indici
vivi, che il C++ non ha: significherebbe duplicare la condizione del cutout, la
famiglia di bug che il progetto evita per regola) o marcare in grigio i numeri
morti (diagnostica, non ergonomia). Rinumerare dentro lo script risolve il
problema dove l'informazione già vive, e non aggiunge codice.

**Il vincolo che ne deriva.** Il clamp resta replicato in due punti — `na`/`nb`
nel corpo, `ca`/`cb` nel cutout — e ora la dipendenza è più stretta di prima: il
cutout conta `ca*cb` assumendo che il corpo abbia numerato su `na`. Se i due
clamp divergono, le patch vive e quelle tagliate non coincidono più. Vale la nota
già presente nel sorgente: vanno tenuti identici.

### Gli altri preset della famiglia

Il difetto è **ancora presente** in `Calabi-Yau`, `Calabi-Yau 3D` e
`Calabi-Yau Solid`, che usano la base 5 fissa: con A=3 le vive sono
`0,1,2, 5,6,7, 10,11,12` e dal quarto numero il selettore cade su patch scartate.
La stessa correzione si applica identica (`mod(mesh, n)` al posto di
`mod(mesh, 5.0)`, cutout su `n*n`), ma non l'ho fatta: sono preset separati e la
richiesta riguardava questo. `Calabi-Yau n5` non è interessato — non ha cutout.

## 6. Se vuoi cambiare il numero di mesh

Il limite di 7×7 = 49 non è del motore, è **scritto in tre posti** del preset che
devono cambiare insieme. Per portarlo a, diciamo, 8×8 = 64:

1. **Aggiungi blocchi `//MESH_BEGIN`** fino ad averne 64. Sono dichiarazioni
   statiche lette al Run: il numero non può dipendere da uno slider.
2. **Cambia la base da 7 a 8** nelle due decomposizioni di `mesh` — quella nel
   corpo (`k1`, `k2`) e quella nel cutout. Devono essere la stessa base.
3. **Alza il tetto dei clamp** da `7.0` a `8.0`, in entrambi i posti (`na`/`nb`
   nel corpo, `ca`/`cb` nel cutout), e il dominio degli slider da `int(2,7)` a
   `int(2,8)` — che va anche nel campo `discreteConstants` del JSON.

`MESH_VISIBLE := A*B;` invece resta com'è: è già espressa in funzione degli
slider.

Tieni presente il costo. Il rapporto fra mesh dichiarate e mesh viste è già
sfavorevole: a A=2, B=2 disegni 4 patch su 49 dichiarate, e le altre 45 le paghi
comunque in generazione di griglia e vertex shader. A 64 il rapporto peggiora, e
la memoria/il tempo di Run crescono anche per chi tiene gli slider al minimo. Se
il preset dovesse diventare pesante su mobile, la strada non è alzare il tetto ma
abbassare il valore dichiarato nei blocchi (il `40`), perché quello è la
proporzione su cui lo slider Steps riscala tutto.

---

## 7. Il rapporto con Calabi-Yau

Sì, il legame è diretto e non è una somiglianza vaga: **Brieskorn-Pham è la
generalizzazione del preset `Calabi-Yau`**, e ne condivide struttura, codice e
impianto multi-mesh quasi riga per riga. Lo dice il commento in testa allo script
stesso.

### La matematica

Il preset Calabi-Yau disegna la **quintica di Fermat**, cioè

```
z1^n + z2^n = 1        un solo esponente n, uguale per entrambe
```

Brieskorn-Pham disegna

```
z1^a + z2^b = 1        due esponenti indipendenti
```

La parametrizzazione è identica — stesse patch da radici dell'unità, stesso
`w = u + iv`, stesso trucco della potenza complessa in forma polare con
`safePow` e `atan`, stessa nota sul taglio di ramo evitato perché l'argomento
resta in (-π/2, π/2).

La differenza non è cosmetica. Con `a ≠ b` la figura **perde la simmetria di
scambio** fra le due coordinate complesse e **cambia genere topologico**: è una
famiglia di superfici diversa, non una deformazione continua della stessa forma.
Con `a = b` ricadi esattamente sulla quintica, quindi Calabi-Yau è il caso
diagonale di Brieskorn-Pham.

Fuori dall'app: le superfici di Brieskorn-Pham sono lo strumento classico per
costruire *sfere esotiche* e studiare singolarità isolate, e la sezione di
Calabi-Yau con n=5 è la figura che compare in tutte le illustrazioni divulgative
sulla teoria delle stringhe.

### Le conseguenze sulle mesh

Tutte le differenze di impianto discendono dall'avere due esponenti invece di uno:

| | Calabi-Yau | Brieskorn-Pham |
|---|---|---|
| esponenti | uno (`A`) | due (`A`, `B`) |
| slider | `A := int(2,5)` | `A := int(2,7)`, `B := int(2,7)` |
| blocchi `//MESH_BEGIN` | 25 = 5×5 | 49 = 7×7 |
| base degli indici | 5 | 7 |
| griglia viva | **quadrata**, A×A | **rettangolare**, A×B |
| `MESH_VISIBLE` | `A*A` | `A*B` |
| cutout | un limite `nc`, usato due volte | due limiti distinti, `ca` e `cb` |

Il punto centrale è l'ultima riga. In Calabi-Yau il cutout confronta riga e
colonna con **lo stesso** limite; in Brieskorn-Pham con due limiti diversi — ed è
proprio lì che la griglia smette di essere quadrata, come dice il commento nel
sorgente. Da qui a cascata: serve un secondo slider, serve `A*B` invece di `A*A`,
e il tetto va portato da 5×5 a 7×7.

Nota che 49 non è "il doppio di lavoro": entrambi i preset dichiarano il massimo
e ne scartano gran parte. Il rapporto peggiora, però — Calabi-Yau al minimo
disegna 4 patch su 25, Brieskorn-Pham 4 su 49.

### Gli altri preset della famiglia

Nella libreria ci sono quattro varianti, utili da distinguere:

- **`Calabi-Yau`** — la 4D vera: `return vec4(z1.x, z2.x, z1.y, z2.y)`, la quarta
  coordinata esce dal vec4 e resta all'interfaccia. È il modello che
  Brieskorn-Pham segue.
- **`Calabi-Yau 3D`** — la quarta dimensione è proiettata *dentro lo script*,
  mescolata nella z dall'angolo B (0.785 ≈ π/4, l'inquadratura canonica), e la
  componente w torna 0. La figura non risponde alle rotazioni 4D.
- **`Calabi-Yau n5`** — grado fisso 5, senza `MESH_VISIBLE`.
- **`Calabi-Yau Solid`** — variante di resa.

Brieskorn-Pham segue deliberatamente il primo: `return vec4(z1.x, z2.x, z1.y, C * z2.y)`.
La quarta coordinata è vera, e in più lo slider **C** la scala — a C=0 la figura
collassa in R³ (comodo per confrontarla con la vista 3D), a C=1 è la superficie
4D piena.

## 8. In sintesi

| Meccanismo | Dove vive | Quando è valutato | A cosa serve |
|---|---|---|---|
| `//MESH_BEGIN` | script, letto dalla CPU | al **Run** | dichiara dominio e proporzione di ogni parte |
| variabile `mesh` | shader (`u_meshIndex`) | a ogni **draw** di una parte | dice allo script quale patch sta disegnando |
| `//CUTOUT` | shader, per pixel | a ogni **frame** | scarta le patch fuori da A e B |
| `MESH_VISIBLE` | script, letto dalla CPU | a ogni **cambio costante** | dice all'interfaccia quante ne sono vive |

Il punto di attrito da ricordare: le mesh **si dichiarano una volta sola e in
numero fisso**, ma **si accendono e si spengono in continuo** con le costanti. Il
preset esiste in questa forma — 49 blocchi identici copiati a mano — proprio
perché il conteggio non può seguire uno slider, mentre il disegno sì.
