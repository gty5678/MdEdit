#pragma once
#include <QtGui>
#include "../resources/resource_manager.h"
namespace mdedit {
// CPU-only render boundary: loaders never own a QPainter or viewport.
class IResourceRenderer {
public:
    virtual ~IResourceRenderer()=default;
    virtual bool supports(const ResourceSnapshot &) const=0;
    virtual void paint(QPainter &,const ResourceSnapshot &,const QRectF &) const=0;
};
class RendererRegistry {
public:
    static void paint(QPainter &,const ResourceSnapshot &,const QRectF &);
};
}
