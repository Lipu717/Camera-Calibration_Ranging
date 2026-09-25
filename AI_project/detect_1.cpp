#include <opencv2/opencv.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace cv;
using namespace std;
namespace fs = std::filesystem;

// 构造以棋盘格中心为原点的三维坐标点，单位 mm，Z 轴朝上（棋盘平面为 XY 面）
static vector<Point3f> centeredBoardPoints(Size boardSize, double squareMm)
{
    vector<Point3f> points;
    for (int row = 0; row < boardSize.height; ++row)
        for (int col = 0; col < boardSize.width; ++col)
            points.emplace_back(
                static_cast<float>((col - (boardSize.width - 1) / 2.0) * squareMm),
                static_cast<float>((row - (boardSize.height - 1) / 2.0) * squareMm), 0.0f);
    return points;
}

// 棋盘格相对相机的位姿：旋转向量 rvec + 平移向量 tvec（单位 mm），rmsPx 为拟合误差
struct BoardPose
{
    Vec3d rvec, tvec; // 平移单位是 mm，因为棋盘点用的是 mm
    double rmsPx = 0;
};

// 用 solvePnP 估计棋盘格相对相机的位姿，并做有效性校验
static bool estimateBoardPose(const vector<Point3f>& objectPoints,//棋盘格内角点的三维坐标
    const vector<Point2f>& imagePoints,//对应图像像素坐标
    const Mat& cameraMatrix, const Mat& distortion,
    BoardPose& pose)
{
    if (objectPoints.size() != imagePoints.size() || objectPoints.size() < 4) return false;
    if (!solvePnP(objectPoints, imagePoints, cameraMatrix, distortion,
        pose.rvec, pose.tvec, false, SOLVEPNP_ITERATIVE)) return false;
    for (int i = 0; i < 3; ++i)
        if (!isfinite(pose.tvec[i]) || !isfinite(pose.rvec[i])) return false;
    if (pose.tvec[2] <= 0) return false;   // 深度必须为正，否则解是物理上不可能的
    
    //重投影误差
    vector<Point2f> projected;
    projectPoints(objectPoints, pose.rvec, pose.tvec, cameraMatrix, distortion, projected);
    pose.rmsPx = norm(imagePoints, projected, NORM_L2) /
        sqrt(static_cast<double>(projected.size()));
    return isfinite(pose.rmsPx);
}

// 从命令行参数里解析一个正数，用于方格边长
static double positiveNumber(const string& text)
{
    istringstream in(text);
    double value = 0;
    if (!(in >> value)) throw runtime_error("请输入一个数字作为方格边长（单位 mm）。");
    in >> ws;
    if (!in.eof() || !isfinite(value) || value <= 0)
        throw runtime_error("方格边长必须是正的有限数值，单位 mm。");
    return value;
}

int main(int argc, char** argv)
{
    //定义常量
    const double measuredSquareMm = 16.0;   // 用户实测的平板棋盘格边长
    const string calibrationPath = argc > 1 ? argv[1] :
        "D:/vs/cv/test/AI_project/camera.yml";
    try
    {
        // 读取标定文件，取出内参、畸变、图像分辨率、棋盘格内角点数、相机索引
        FileStorage file(calibrationPath, FileStorage::READ);
        if (!file.isOpened()) throw runtime_error("无法打开标定文件: " + calibrationPath);
        Mat cameraMatrix, distortion;
        int imageWidth = 0, imageHeight = 0, cameraIndex = 0, cols = 0, rows = 0;
        file["camera_matrix"] >> cameraMatrix;
        file["distortion_coefficients"] >> distortion;
        file["image_width"] >> imageWidth;
        file["image_height"] >> imageHeight;
        file["board_columns"] >> cols;
        file["board_rows"] >> rows;
        if (!file["camera_index"].empty()) file["camera_index"] >> cameraIndex;
        file.release();

        // 校验读取到的参数是否齐全、格式是否正确
        if (cameraMatrix.rows != 3 || cameraMatrix.cols != 3 ||
            cameraMatrix.channels() != 1 || distortion.empty() ||
            imageWidth <= 0 || imageHeight <= 0 || cols < 3 || rows < 3)
            throw runtime_error("标定文件缺少必要的相机/棋盘格参数。");
        cameraMatrix.convertTo(cameraMatrix, CV_64F);
        distortion.convertTo(distortion, CV_64F);
        if (!checkRange(cameraMatrix) || !checkRange(distortion) ||
            cameraMatrix.at<double>(0, 0) <= 0 || cameraMatrix.at<double>(1, 1) <= 0)
            throw runtime_error("标定参数中含有无效数值。");

        cout << "已加载: " << calibrationPath << '\n'
            << "需要的分辨率: " << imageWidth << " x " << imageHeight << '\n'
            << "棋盘格: " << cols << " x " << rows << " 个内角点。\n"
            << "请测量 5 个相邻方格的总长度，再除以 5。\n"
            << "对于平板显示棋盘格，量完尺寸后不要再改变缩放。\n";
        
        //测量的方格边长
        const double squareMm = argc > 2 ? positiveNumber(argv[2]) : measuredSquareMm;
        
        // 创建棋盘格三维点
        const Size boardSize(cols, rows), expectedSize(imageWidth, imageHeight);
        const auto objectPoints = centeredBoardPoints(boardSize, squareMm);

        // 打开摄像头    
        VideoCapture cap(cameraIndex);
        if (!cap.isOpened()) throw runtime_error("无法打开摄像头，请关闭其他占用摄像头的程序。");
        cap.set(CAP_PROP_FRAME_WIDTH, imageWidth);
        cap.set(CAP_PROP_FRAME_HEIGHT, imageHeight);

        cout << "使用方格边长: " << squareMm << " mm。\n"
            << "测距目标 = 棋盘格中心。Z = 沿光轴的深度。\n"
            << "Distance = 到中心的直线距离。显示单位：cm。\n"
            << "S: 保存当前有效测量值和图像；R: 保存原始诊断图像；Esc: 退出。\n";

        //输出目录和 CSV 日志
        const fs::path runDir = fs::current_path() / ("ranging_" + to_string(getTickCount()));
        fs::create_directories(runDir);
        ofstream log(runDir / "measurements.csv");
        if (!log) throw runtime_error("无法写入测量日志。");
        log << "image,elapsed_ms,square_mm,x_cm,y_cm,z_cm,distance_cm,reprojection_rms_px\n";
        log << fixed << setprecision(6);
        cout << "输出目录: " << runDir << '\n';

        const int64 start = getTickCount();
        int saved = 0, debug = 0;

        // 主循环
        while (true)
        {
            Mat frame, gray;
            if (!cap.read(frame) || frame.empty()) throw runtime_error("摄像头返回了空帧。");

            // 分辨率必须和标定时一致，否则内参不适用，测距会严重偏差
            if (frame.size() != expectedSize)
                throw runtime_error("摄像头分辨率与标定时不一致。当前为 " +
                    to_string(frame.cols) + "x" + to_string(frame.rows) + "，期望 " +
                    to_string(imageWidth) + "x" + to_string(imageHeight) +
                    "。请使用标定时的分辨率模式，不要盲目缩放图像。");

            cvtColor(frame, gray, COLOR_BGR2GRAY);
            vector<Point2f> corners;
            bool found = findChessboardCornersSB(gray, boardSize, corners, CALIB_CB_NORMALIZE_IMAGE);

            Mat preview = frame.clone();
            BoardPose pose;
            const bool valid = found && estimateBoardPose(objectPoints, corners, cameraMatrix, distortion, pose);

            putText(preview, "目标: 棋盘格中心 | S: 保存 R: 原图 Esc: 退出",
                Point(10, 23), FONT_HERSHEY_SIMPLEX, 0.48, Scalar(255, 255, 255), 1, LINE_AA);

            if (valid)
            {
                drawChessboardCorners(preview, boardSize, corners, true);

                // 把棋盘格原点 (0,0,0) 投影到图像上，就是棋盘格中心的像素位置
                vector<Point2f> center;
                projectPoints(vector<Point3f>{Point3f(0, 0, 0)}, pose.rvec, pose.tvec,
                    cameraMatrix, distortion, center);
                circle(preview, center[0], 6, Scalar(0, 255, 0), 2);

                const double distanceCm = norm(pose.tvec) / 10.0;
                putText(preview, format("Z 深度: %.1f cm | 距离: %.1f cm",
                    pose.tvec[2] / 10.0, distanceCm),
                    Point(10, 48), FONT_HERSHEY_SIMPLEX, 0.6, Scalar(0, 255, 0), 2, LINE_AA);
                putText(preview, format("X: %.1f cm  Y: %.1f cm | 拟合 RMS: %.3f px",
                    pose.tvec[0] / 10.0, pose.tvec[1] / 10.0, pose.rmsPx),
                    Point(10, 72), FONT_HERSHEY_SIMPLEX, 0.5, Scalar(0, 255, 255), 1, LINE_AA);
            }
            else
            {
                putText(preview, "未检测到有效棋盘格，请让完整图案清晰可见",
                    Point(10, 50), FONT_HERSHEY_SIMPLEX, 0.48, Scalar(0, 180, 255), 1, LINE_AA);
            }

            imshow("Camera ranging", preview);
            const int key = waitKey(1) & 0xff;
            if (key == 27) break;

            // R: 保存当前原始帧，便于事后排查（不参与测距）
            if (key == 'r' || key == 'R')
            {
                const fs::path path = runDir / ("debug_" + to_string(++debug) + ".png");
                if (!imwrite(path.string(), frame)) throw runtime_error("无法保存原始帧。");
                cout << "原始帧已保存: " << path << '\n';
            }

            // S: 保存当前有效测量：图像 + 日志
            if (key == 's' || key == 'S')
            {
                if (!valid) { cout << "当前没有有效的测量结果。按 R 可保存原始诊断图像。\n"; continue; }
                const string stem = "measurement_" + to_string(++saved);
                if (!imwrite((runDir / (stem + "_raw.png")).string(), frame) ||
                    !imwrite((runDir / (stem + ".png")).string(), preview))
                    throw runtime_error("无法保存测量图像。");
                log << stem << ".png," << (getTickCount() - start) * 1000.0 / getTickFrequency()
                    << ',' << squareMm << ',' << pose.tvec[0] / 10.0 << ',' << pose.tvec[1] / 10.0
                    << ',' << pose.tvec[2] / 10.0 << ',' << norm(pose.tvec) / 10.0 << ',' << pose.rmsPx << '\n';
                log.flush();
                if (!log) throw runtime_error("测量日志写入失败。");
                cout << "已保存 " << stem << ": 距离=" << norm(pose.tvec) / 10.0 << " cm\n";
            }
        }
        return 0;
    }
    catch (const exception& e)
    {
        cerr << "错误: " << e.what() << '\n';
        return 1;
    }
}