import cv2
import os

print(cv2.__file__)
face_cascade = cv2.CascadeClassifier('/home/ruby/Desktop/case/haarcascade_frontalface_default.xml')
recognizer = cv2.face.LBPHFaceRecognizer_create()

save_model = '/home/ruby/Desktop/model' # 학습한 모델을 가져와야하기 때문에 학습된 모델이 저장된 directory 가져옴
recognizer.read(os.path.join(save_model, 'trainer.yml'))


predict_dir =  '/home/ruby/Desktop/image'
font = cv2.FONT_HERSHEY_SIMPLEX

# 가장 최근 사진 가져오기
file_name = None
image_files = [f for f in os.listdir(predict_dir) if f.lower().endswith(('.png', '.jpg', '.jpeg'))]
if image_files:
    # 수정 시간을 기준으로 정렬 후 가장 최근 파일 선택
    file_name = max(image_files, key=lambda f: os.path.getmtime(os.path.join(predict_dir, f)))

print(file_name)
if file_name is None:
    print(f"{file_name} 디렉토리에 사진이 없습니다.")
else:
    img_path = os.path.join(predict_dir, file_name) # 파이 4에서 찍은 이미지 파일 읽어옴
    img = cv2.imread(img_path)
    if img is None:
        print(f"이미지 파일을 읽을 수 없습니다 : {file_name}")
    else:
        gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY) # 그레이 스케일로 변환 
        faces = face_cascade.detectMultiScale( 
            gray,
            scaleFactor = 1.05,
            minNeighbors = 7,
            minSize = (70,70)
        )
        if len(faces) >= 1:
            if len(faces) > 1:
                faces = sorted(faces, key = lambda x: x[2] * x[3], reverse=True)
                largest_face = faces[0]
                x, y, w, h = largest_face
            elif len(faces) == 1:
                x, y, w, h = faces[0]
            cv2.rectangle(img, (x,y), (x+w,y+h), (0,255,0), 2) # imshow함수에서 사진 보여줄 때 사각형으로 얼굴 보이도록
            id, confidence = recognizer.predict(gray[y:y+h,x:x+w])  # predict 함수의 유사도 반환값을 0에 가까울 수록 유사도가 높아서 100 보다 크면 유사한 사람이 없다 판정
            confidence_str = "  {0}%".format(round(100 - confidence))
            print(100-confidence)
            if ((100 - confidence) > 85): # 유사도가 우리가 보는 것과 반대로 반환되므로 우리가 보기 쉽게 100에서 뺀 값으로 설정
                print("Correct")
                with open("/home/ruby/Desktop/recognize_student.txt","w") as file: # 사생 학번 텍스트 파일로 저장
                    file.write(f"m{id}\n")
            else:
                print("Fale")
                id = "unknown"
                confidence_str = "  {0}%".format(round(100 - confidence))
                with open("/home/ruby/Desktop/recognize_student.txt","w") as file: # 사생 학번 텍스트 파일로 저장
                        file.write(f"m{id}\n")
                
                cv2.putText(img, str(id), (x+5,y-5), font, 1, (255,255,255), 2) # 얼굴 인식 결과를 보여주기 위한 셋업
                cv2.putText(img, str(confidence_str), (x+5,y+h-5), font, 1, (255,255,0), 1)  # 얼굴 인식 결과를 보여주기 위한 셋업
            resize_img = cv2.resize(img,(640,480))
            cv2.imshow('camera',resize_img) # 이미지 인식한 결과를 사진으로 보여줌 + 얼굴에 사각형 표시와 유사한 사람의 학번, 유사도와 함께 보여줌
           # k = cv2.waitKey(5000) & 0xff 
            cv2.waitKey(5000)
            cv2.destroyAllWindows()
        else:
            with open("/home/ruby/Desktop/recognize_student.txt","w") as file: # 사생 학번 텍스트 파일로 저장
                    file.write(f"notpeople\n")
print("\n [INFO] Exiting Program and cleanup stuff")

cv2.destroyAllWindows()
