// SPDX-License-Identifier: MIT

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QUrl>

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QQmlApplicationEngine engine;
  engine.load(QUrl::fromLocalFile(QStringLiteral(OA_DASHBOARD_QML_FILE)));

  if (engine.rootObjects().isEmpty()) {
    return 1;
  }

  return QGuiApplication::exec();
}
