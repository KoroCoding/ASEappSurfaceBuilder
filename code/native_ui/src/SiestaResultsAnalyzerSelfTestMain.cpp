#include "SiestaResultsAnalyzer.h"

#include <QApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include <cmath>
#include <iostream>

namespace {
bool near(double a, double b) { return std::abs(a - b) < 1.0e-10; }
}

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QTemporaryDir directory;
    if (!directory.isValid()) return 1;

    const QString path = directory.filePath(QStringLiteral("sample.out"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return 2;
    QTextStream out(&file);
    out << "SIESTA Version: 5.2.0\n"
           "SystemName demo-system\n"
           "MaxSCFIterations 100\n"
           "DM.Tolerance 1.0e-4\n"
           "MD.MaxForceTol 0.04 eV/Ang\n"
           "Begin CG opt. move = 1\n"
           "scf: 1 -10.0 -11.0 -12.0 1.0e-2 0.5 0.1\n"
           "scf: 2 -11.0 -12.0 -13.0 1.0e-5 0.4 0.01\n"
           "SCF cycle converged after 2 iterations\n"
           "siesta: E_KS(eV) = -12.0\n"
           "siesta: Atomic forces (eV/Ang):\n"
           "Res 0.02\n"
           "Max 0.05\n"
           "Max 0.03 constrained\n"
           "Stress tensor Voigt[x,y,z,yz,xz,xy] (kbar): 1 2 3 0 0 0\n"
           "outcell: Cell vector modules (Ang) : 3 4 5\n"
           "outcell: Cell angles (23,13,12) (deg): 90 90 120\n"
           "outcell: Cell volume (Ang**3) : 51.96\n"
           "End of run\n"
           "Job completed\n";
    file.close();

    QString error;
    const SiestaAnalysisResult result = SiestaResultsParser::parseFile(path, &error);
    const bool okay = error.isEmpty()
        && result.systemName == QStringLiteral("demo-system")
        && result.normalEnd
        && result.status == QStringLiteral("FINISHED")
        && result.scf.size() == 2
        && result.geometry.size() == 1
        && result.geometry.front().scfConverged
        && result.geometry.front().scfIterations == 2
        && result.geometry.front().hasEnergy
        && near(result.geometry.front().energyEv, -12.0)
        && result.geometry.front().hasForce
        && near(result.geometry.front().maxForceEvAng, 0.03)
        && near(result.geometry.front().rawMaxForceEvAng, 0.05)
        && near(result.geometry.front().constrainedMaxForceEvAng, 0.03)
        && near(result.geometry.front().residualForceEvAng, 0.02)
        && result.geometry.front().hasStress
        && near(result.geometry.front().maxStressKbar, 3.0)
        && result.geometry.front().hasCellLengths
        && result.geometry.front().hasCellAngles
        && result.geometry.front().hasCellVolume
        && near(result.geometry.front().cellVolumeAng3, 51.96)
        && result.scf.back().globalIteration == 2
        && result.scf.back().hasHError
        && near(result.dmTolerance, 1.0e-4)
        && near(result.forceToleranceEvAng, 0.04);
    if (!okay) {
        std::cerr << "SIESTA parser self-test failed: " << error.toStdString()
                  << " system=" << result.systemName.toStdString()
                  << " status=" << result.status.toStdString()
                  << " scf=" << result.scf.size()
                  << " geometry=" << result.geometry.size()
                  << " dmTol=" << result.dmTolerance
                  << " forceTol=" << result.forceToleranceEvAng << '\n';
        return 3;
    }
    SiestaResultsDialog dialog;
    dialog.addFiles({path});
    dialog.show();
    app.processEvents();
    if (dialog.grab().isNull()) {
        std::cerr << "SIESTA analyzer UI render failed\n";
        return 4;
    }
    dialog.close();
    std::cout << "SIESTA parser self-test passed\n";
    return 0;
}
