#pragma once

#include "pdfrecognizer.h"

#include <QString>
#include <functional>

class AudiverisBridge {
public:
    static bool available(QString *description=nullptr);
    static bool ensureAvailable(QString *error=nullptr,
                                std::function<void(int,const QString&)> progress={},
                                std::function<bool()> canceled={});
    static PdfRecognitionResult recognize(const QString &pdfPath,int fallbackTempo,
                                           std::function<bool()> canceled={});
};
