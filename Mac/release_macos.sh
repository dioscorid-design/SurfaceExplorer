#!/bin/bash
set -e

# --- CONFIGURAZIONE ---
APP_NAME="SurfaceExplorer"
BUNDLE_ID="com.dioscorid.surfaceexplorer"
SIGN_IDENTITY="Developer ID Application: GAETANO MOSCHETTI (BAPKX72394)"
NOTARY_PROFILE="notarytool-profile"

# Lo script vive in Mac/ : la build sta nella root del progetto (una cartella sopra)
BUILD_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/build/Desktop_Qt_6_10-Release"
APP_PATH="$BUILD_DIR/$APP_NAME.app"
DMG_NAME="$APP_NAME.dmg"
DMG_PATH="$BUILD_DIR/$DMG_NAME"

ENTITLEMENTS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/macos_release.entitlements"

if [ ! -d "$APP_PATH" ]; then
  echo "ERRORE: $APP_PATH non trovato. Compila prima il progetto (macdeployqt incluso)."
  exit 1
fi

echo ">>> 1. Firma con Developer ID + Hardened Runtime..."
codesign --force --deep --options runtime \
  --entitlements "$ENTITLEMENTS" \
  --sign "$SIGN_IDENTITY" \
  "$APP_PATH"

echo ">>> 2. Verifica firma..."
codesign --verify --deep --strict --verbose=2 "$APP_PATH"

echo ">>> 3. Creazione .dmg..."
rm -f "$DMG_PATH"
hdiutil create -volname "$APP_NAME" -srcfolder "$APP_PATH" -ov -format UDZO "$DMG_PATH"

echo ">>> 4. Firma del .dmg..."
codesign --force --sign "$SIGN_IDENTITY" "$DMG_PATH"

echo ">>> 5. Invio a notarizzazione (attende il risultato, puo' richiedere alcuni minuti)..."
xcrun notarytool submit "$DMG_PATH" --keychain-profile "$NOTARY_PROFILE" --wait

echo ">>> 6. Staple del ticket di notarizzazione..."
xcrun stapler staple "$DMG_PATH"

echo ""
echo "=== FATTO ==="
echo "DMG pronto e notarizzato: $DMG_PATH"
echo "Verifica finale con: spctl -a -t open --context context:primary-signing-identifier -v \"$DMG_PATH\""
