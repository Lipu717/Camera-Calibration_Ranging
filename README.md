# 基于计算机视觉完成的相机标定和测距
## 文件夹说明
```AI_project/
├── .github/
│   └── copilot-instructions.md
├── .vs/
│   └── AI_project/
│       ├── CopilotIndices/
│       │   └── 17.14.1661.41761/
│       │       ├── CodeChunks.db
│       │       └── SemanticSymbols.db
│       ├── FileContentIndex/
│       │   ├── 817e3cbb-bb1c-42b4-aa09-951d88c9bbd8.vsidx
│       │   ├── a2d2cca4-8027-4b9b-a342-70c9af2deadc.vsidx
│       │   ├── af24b29a-8174-461b-ba8e-05c645fae8d4.vsidx
│       │   └── b34b67f0-f5f7-4d74-9000-90884eb792dc.vsidx
│       ├── copilot-chat/
│       │   └── a58d16a4/
│       │       └── sessions/
│       └── v17/
│           ├── ipch/
│           │   └── AutoPCH/
│           ├── .suo
│           ├── Browse.VC.db
│           ├── DocumentLayout.backup.json
│           ├── DocumentLayout.json
│           └── Solution.VC.db
├── AI_project/
│   └── x64/
│       └── Debug/
│           ├── AI_project.tlog/
│           │   ├── AI_project.lastbuildstate
│           │   ├── CL.command.1.tlog
│           │   ├── CL.read.1.tlog
│           │   ├── CL.write.1.tlog
│           │   ├── Cl.items.tlog
│           │   ├── link.command.1.tlog
│           │   ├── link.read.1.tlog
│           │   ├── link.secondary.1.tlog
│           │   └── link.write.1.tlog
│           ├── AI_project.exe.recipe
│           ├── AI_project.ilk
│           ├── AI_project.log
│           ├── detect.obj
│           ├── detect_1.obj
│           ├── vc143.idb
│           └── vc143.pdb
├── x64/
│   └── Debug/
│       ├── AI_project.exe
│       └── AI_project.pdb
├── AI_project.sln
├── AI_project.vcxproj
├── AI_project.vcxproj.filters
├── AI_project.vcxproj.user
├── camera.yml
├── detect.cpp
└── detect_1.cpp
```
## 整体思路
整个项目分两步：<br>
1.标定：拿一个已知尺寸的棋盘格，从不同角度拍很多张，用张正友标定法解出相机的内参矩阵和畸变系数，保存成 camera.yml。<br> 
2.测距：读入 camera.yml，实时检测画面里的棋盘格，用 solvePnP 解出棋盘格相对相机的三维位姿，从而得到棋盘格中心到相机的距离。
核心逻辑：标定是一次性的，测距是实时的。 不标定就没法测距，因为 PnP 求解需要内参和畸变作为输入。

## 程序一（detect.cpp）：相机标定<br>
### 目的<br>
求出相机的内参矩阵和畸变系数，让后续程序能把像素坐标和真实世界坐标对应起来。<br>
### 原理<br>
使用张正友标定法，OpenCV 对应函数 calibrateCamera。<br>
1.棋盘格的每个内角点在棋盘格坐标系下的三维坐标是已知的（方格边长固定）。<br>
2.在图像里的像素坐标由 findChessboardCornersSB 检测得到。<br>
3.采集足够多不同角度的视图后，联立所有视图的对应关系，解出内参和畸变。<br>
### 流程<br>
```
打开摄像头
    ↓
循环：
    读帧 → 转灰度 → 检测棋盘格内角点
    ↓
    按键：
        S → 保存当前视图（角点坐标 + 图像）
        R → 保存原始诊断图像（不参与标定）
        C → 用已采集的视图执行标定
        Esc → 退出
    ↓
标定：
    构造棋盘格三维坐标
    calibrateCamera 求解
    计算每视图重投影误差
    保存 camera.yml
```
## 关键函数<br>
findChessboardCornersSB：亚像素级棋盘格检测。<br>
calibrateCamera：核心标定函数。<br>
projectPoints：把三维点投影回图像，用于算重投影误差。<br>

## 程序二（detect_1.cpp）：棋盘格测距<br>
### 目的<br>
用标定好的相机，实时测量棋盘格中心相对相机的三维位置。<br>
### 原理<br>
用 solvePnP 解 PnP 问题：已知 n 个三维点及其在图像上的投影，求相机的位姿。<br>
三维点：棋盘格内角点在棋盘格坐标系下的坐标（单位 mm）。<br>
投影点：图像上检测到的角点像素坐标。<br>
输出：旋转向量 rvec + 平移向量 tvec。<br>
tvec 就是棋盘格中心在相机坐标系下的坐标。除以 10 换算成厘米，就是实际距离。<br>

### 流程<br>
```
text
读取 camera.yml
    ↓
校验内参、畸变、分辨率、棋盘格规格
    ↓
打开摄像头，设置分辨率
    ↓
循环：
    读帧 → 检查分辨率一致 → 转灰度 → 检测棋盘格
    ↓
    如果检测到：
        solvePnP 解位姿
        校验（有限值、Z > 0）
        算重投影 RMS
        ↓
    如果有效：
        画角点、投影棋盘格中心
        显示 X/Y/Z/距离/RMS
    ↓
    按键：
        S → 保存测量结果（图像 + CSV）
        R → 保存原始诊断图像
        Esc → 退出
```
### 关键函数<br>
solvePnP：核心位姿求解。<br>
projectPoints：把棋盘格中心投影回图像，画出绿圈。<br>
norm(tvec)：平移向量的模长，即直线距离。<br>

### 输出文件<br>
每次运行生成 ranging_<时间戳>/ 目录。<br>
measurements.csv：所有按 S 保存的测量记录。<br>
measurement_N.png：带标注的预览图。<br>
measurement_N_raw.png：原始帧。<br>
debug_N.png：按 R 保存的诊断图。<br>
