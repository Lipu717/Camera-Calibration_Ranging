//#include <opencv2/opencv.hpp>
//#include <cmath>
//#include <filesystem>
//#include <iostream>
//#include <string>
//#include <vector>
//
//using namespace cv;
//using namespace std;
//namespace fs = std::filesystem;
//
//static bool detectBoard(const Mat& gray, const Size& boardSize,
//    vector<Point2f>& corners, bool exhaustive = false)
//{
//    corners.clear();
//    int flags = CALIB_CB_NORMALIZE_IMAGE;
//    if (exhaustive) flags |= CALIB_CB_EXHAUSTIVE | CALIB_CB_ACCURACY;//这里为了更准选择了较慢的检测模式
//    // OpenCV 提供的亚像素级棋盘格检测函数 findChessboardCornersSB
//    const bool found = findChessboardCornersSB(gray, boardSize, corners, flags);
//    return found && corners.size() == static_cast<size_t>(boardSize.area());
//}
//
//int main()
//{
//    //摄像头初始化和输出目录
//    const int cameraIndex = 0;
//    const Size boardSize(9, 6);
//    const float squareSizeMm = 16.0f;//手动测量棋盘格方格边长
//    const size_t minViews = 15;
//
//    try
//    {
//        VideoCapture cap(cameraIndex);
//        if (!cap.isOpened())
//        {
//            cerr << "无法打开摄像头\n";
//            return 1;
//        }
//
//        // 保持摄像头的当前分辨率，使用相同的分辨率进行测距
//        const fs::path runDir = fs::current_path() /
//            ("calibration_" + to_string(getTickCount()));//输出目录用时间戳命名，避免覆盖
//        fs::create_directories(runDir);//创建输出目录
//
//        cout << "输出目录: " << runDir << '\n'
//            << "棋盘格: 9 x 6 个内角点; 方格边长: " << squareSizeMm << " mm.\n"
//            << "S: 检测并保存有效视图。R: 随时保存原始诊断图像。\n"
//            << "C: 收集至少 15 个不同视角后开始标定。\n"
//            << "Esc: 退出。拍摄间隙请移动并倾斜棋盘格。\n";
//
//        // 初始化状态变量
//        vector<vector<Point2f>> imagePoints;
//        Size imageSize;//图像分辨率
//        string status = "显示整个棋盘，包括其白色边框。";//当前提示文字
//        int debugImageCount = 0;
//        Mat frame;//当前帧的缓冲区
//
//        // 主循环
//        while (true)
//        {
//            if (!cap.read(frame) || frame.empty())
//            {
//                cerr << "未收到当前帧.\n";
//                return 1;
//            }
//            if (imageSize.empty())
//            {
//                imageSize = frame.size();
//                cout << "实际分辨率: " << imageSize.width << " x "
//                    << imageSize.height << '\n';
//            }
//            if (frame.size() != imageSize)
//            {
//                cerr << "分辨率已更改，请在固定分辨率下重新校准。\n";
//                return 1;
//            }
//
//            //采集视图
//            Mat gray, preview = frame.clone();
//            cvtColor(frame, gray, COLOR_BGR2GRAY);
//            
//            vector<Point2f> corners;
//            bool found = detectBoard(gray, boardSize, corners);
//            if (found)
//            {
//                // 在预览图上显示状态信息
//                drawChessboardCorners(preview, boardSize, corners, true);
//            }
//
//            // 显示于页面顶部
//            const string top = string(found ? "FOUND" : "NOT FOUND") + " | views: " +
//                to_string(imagePoints.size()) + " | S: save R: raw C: calibrate Esc: exit";
//            // 显示当前状态
//            putText(preview, top, Point(10, 25), FONT_HERSHEY_SIMPLEX, 0.5,
//                found ? Scalar(0, 255, 0) : Scalar(0, 180, 255), 1, LINE_AA);
//            putText(preview, status, Point(10, 50), FONT_HERSHEY_SIMPLEX, 0.45,
//                Scalar(0, 255, 255), 1, LINE_AA);
//            imshow("Camera calibration", preview);
//
//            const int key = waitKey(1) & 0xff;
//            if (key == 27) break;
//
//            // 按 R 保存原始图像，原始图像仅用于排查问题，不参与标定
//            if (key == 'r' || key == 'R')
//            {
//                const fs::path debugPath = runDir /
//                    ("debug_" + to_string(++debugImageCount) + ".png");
//                if (!imwrite(debugPath.string(), frame))
//                {
//                    cerr << "无法保存原始图像: " << debugPath << '\n';
//                    status = "原始图像写入失败，请查看控制台";
//                    continue;
//                }
//                cout << "已保存原始诊断图像（不作为标定视图）: "
//                    << debugPath << '\n';
//                status = "原始图像已保存，路径见控制台，不计入视图数";
//            }
//
//            //按 S 保存视图
//            if (key == 's' || key == 'S')
//            {
//                // 普通模式没检测到，尝试更慢的深度检测
//                if (!found)
//                {
//                    cout << "正在对当前帧尝试更深入的检测...\n";
//                    found = detectBoard(gray, boardSize, corners, true);
//                }
//                if (!found)
//                {
//                    status = "未检测到完整的 9x6 棋盘格。按 R 保存诊断图像";
//                    cout << "未保存为标定视图：未检测到 54 个内角点\n"
//                        << "请检查棋盘格是否完整可见、白色边距、对焦情况，以及是否为 10x7 个方格\n"
//                        << "按 R 保存原始摄像头图像以便排查\n";
//                    continue;
//                }
//
//                // 重复视图检查；视图多样性需要自己把握
//                bool duplicate = false;
//                for (const auto& oldCorners : imagePoints)
//                {
//                    const double rmsMotion = norm(corners, oldCorners, NORM_L2) /
//                        sqrt(static_cast<double>(corners.size()));
//                    if (rmsMotion < 3.0) { duplicate = true; break; }
//                }
//                if (duplicate)
//                {
//                    status = "视图过于相似：请移动或倾斜棋盘格后再保存";
//                    cout << "未保存：该视图与之前的视图过于相似\n";
//                    continue;
//                }
//                // 保存视图图像
//                const fs::path shot = runDir /
//                    ("view_" + to_string(imagePoints.size() + 1) + ".png");
//                if (!imwrite(shot.string(), frame))
//                {
//                    cerr << "无法保存图像: " << shot << '\n';
//                    return 1;
//                }
//                // 记录角点坐标
//                imagePoints.push_back(corners);
//                status = "已保存。请改变棋盘格位置、距离和倾斜角度";
//                cout << "已保存视图 " << imagePoints.size() << '\n';
//            }
//
//            //按 C 标定
//            if (key == 'c' || key == 'C')
//            {
//                if (imagePoints.size() < minViews)
//                {
//                    status = "请先采集至少 15 个不同角度、清晰的视图";
//                    cout << "需要至少 15 个有效视图；当前为 " << imagePoints.size()
//                        << " 个。原始诊断图像不计入\n";
//                    continue;
//                }
//
//                // 构造棋盘格上每个内角点的真实三维坐标
//                // 棋盘格是平面，Z 坐标全为 0
//                vector<Point3f> boardPoints;
//                for (int row = 0; row < boardSize.height; ++row)
//                    for (int col = 0; col < boardSize.width; ++col)
//                        boardPoints.emplace_back(col * squareSizeMm, row * squareSizeMm, 0.0f);
//                
//                // 每个视图共用同一组真实坐标
//                vector<vector<Point3f>> objectPoints(imagePoints.size(), boardPoints);
//                Mat cameraMatrix, distCoeffs;
//                vector<Mat> rvecs, tvecs;
//                cout << "Calibrating...\n";
//
//                // 核心标定函数，返回重投影误差的 RMS
//                const double rms = calibrateCamera(objectPoints, imagePoints, imageSize,
//                    cameraMatrix, distCoeffs, rvecs, tvecs);
//                if (!isfinite(rms) || !checkRange(cameraMatrix) || !checkRange(distCoeffs) ||
//                    cameraMatrix.at<double>(0, 0) <= 0 || cameraMatrix.at<double>(1, 1) <= 0)
//                {
//                    status = "Invalid result: recollect varied views and restart.";
//                    cerr << status << '\n';
//                    continue;
//                }
//
//                //计算每张视图的重投影误差
//                vector<double> perViewErrors;
//                for (size_t i = 0; i < imagePoints.size(); ++i)
//                {
//                    vector<Point2f> projected;
//                    projectPoints(boardPoints, rvecs[i], tvecs[i],
//                        cameraMatrix, distCoeffs, projected);
//                    const double error = norm(imagePoints[i], projected, NORM_L2) /
//                        sqrt(static_cast<double>(projected.size()));
//                    perViewErrors.push_back(error);
//                    cout << "View " << i + 1 << ": " << error << " px RMS\n";
//                }
//
//                // 保存标定结果到 YAML 文件
//                const fs::path output = runDir / "camera.yml";
//                FileStorage file(output.string(), FileStorage::WRITE);
//                if (!file.isOpened())
//                {
//                    cerr << "Cannot write: " << output << '\n';
//                    return 1;
//                }
//                file << "camera_index" << cameraIndex;
//                file << "image_width" << imageSize.width << "image_height" << imageSize.height;
//                file << "board_columns" << boardSize.width << "board_rows" << boardSize.height;
//                file << "square_size_mm" << squareSizeMm;
//                file << "view_count" << static_cast<int>(imagePoints.size());
//                file << "camera_matrix" << cameraMatrix << "distortion_coefficients" << distCoeffs;
//                file << "rms_reprojection_error_px" << rms << "per_view_rms_px" << perViewErrors;
//                file << "image_points" << "[";
//                for (const auto& points : imagePoints) file << points;
//                file << "]";
//                file.release();
//
//                cout << "\n相机内参矩阵:\n" << cameraMatrix
//                    << "\n畸变系数:\n" << distCoeffs
//                    << "\n整体重投影 RMS: " << rms << " px"
//                    << "\n已保存至: " << output << '\n'
//                    << "注意：RMS 低并不能证明真实世界测距一定准确。\n";
//
//                // RMS > 1 像素通常提示有模糊或板子不平整
//                if (rms > 1.0)
//                    cout << "RMS 大于 1 像素：请检查模糊、棋盘格平整度和各视图误差。\n";
//            }
//        }
//        destroyAllWindows();
//        return 0;
//    }
//    catch (const exception& e)
//    {
//        cerr << "Error: " << e.what() << '\n';
//        return 1;
//    }
//}
