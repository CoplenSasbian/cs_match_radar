#include "pch.h"

// 生成文件必须紧跟在定义实现类型的头文件之后编译：
// .g.h 只声明模板基类，实现结构体在 ViewModels.h 中定义，
// 而 .g.cpp 需要这两者都可见才能完成 factory 与构造函数注册。
#include "ViewModels.h"
#include "PlayerCard.xaml.h"

#include "MatchRow.g.cpp"
#include "PlayerRow.g.cpp"
#include "PerfectMatchRow.g.cpp"
#include "PerfectMatchPlayerRow.g.cpp"

// 雷达页面用的玩家行视图。
#include "RadarPlayer.g.cpp"

// 人物卡片使用的三个简单行视图，实现类型定义在 PlayerCard.xaml.h。
// 这里补上它们的 factory 与构造函数注册。
#include "StatItem.g.cpp"
#include "WeaponItem.g.cpp"
#include "MapItem.g.cpp"
