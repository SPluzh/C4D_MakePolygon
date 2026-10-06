#ifndef MAKEPOLYGON_COMMAND_H__
#define MAKEPOLYGON_COMMAND_H__

#include "c4d.h"

namespace cinema
{

#define PLUGIN_ID_MAKEPOLYGON 1067832

class MakePolygonCommand : public CommandData
{
public:
    virtual Bool Execute(BaseDocument* doc, GeDialog* parentManager) override;
    virtual Int32 GetState(BaseDocument* doc, GeDialog* parentManager) override;
};

Bool RegisterMakePolygonCommand();

} // namespace cinema

#endif // MAKEPOLYGON_COMMAND_H__
