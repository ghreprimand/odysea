// Runner for the rendered-shell QML tests.
//
// This executable links the OdySea shell module, so the scenes under test
// resolve `import OdySea` through the module registry the application uses. A
// QML file that the module does not export therefore fails here exactly as it
// would fail at startup.
#include <QtQuickTest>

QUICK_TEST_MAIN(shell)
