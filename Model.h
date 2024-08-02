#pragma once

#include "structs.h"
#include <string>

ModelData LoadObjFile(const std::string& _directoryPath, const std::string& _filename);

MaterialData LoadMaterialTemplateFile(const std::string& _directoryPath, const std::string& _filename);