import cv2
import numpy as np
from PIL import Image
import os
import random

face_cascade = cv2.CascadeClassifier('/home/ruby/Desktop/case/haarcascade_frontalface_default.xml')

input_dir = "/home/ruby/Desktop/case/dataset/extracted/" # 파이1에서 찍은 사진들을 저장한 directory
output_dir = "/home/ruby/Desktop/case/dataset/face_dataset" # 가공된 dataset을 저장할 directory
save_model_dir = '/home/ruby/Desktop/model' # 학습 모델을 저장할 directory

recognizer = cv2.face.LBPHFaceRecognizer_create()

if not os.path.exists(output_dir): # dataset 저장할 디렉토리 없을 때 생성
    os.makedirs(output_dir)


def augment_image(img): # 이미지 증강, 파이1에서 보낸 사진들을 이미지 증강해 사진 추가 -> 기존에 50장 찍었으면 증강 시키면 100장 존재
    # 1. 회전
    angle = random.randint(-30, 30)  # -30도에서 30도 사이로 회전
    M = cv2.getRotationMatrix2D((img.shape[1] // 2, img.shape[0] // 2), angle, 1)
    rotated_img = cv2.warpAffine(img, M, (img.shape[1], img.shape[0]))

    # 2. 수평 반전
    flip_img = cv2.flip(rotated_img, 1)

    # 3. 밝기 조정
    brightness = random.randint(-50, 50)
    bright_img = cv2.convertScaleAbs(flip_img, alpha=1, beta=brightness)

    return bright_img

for idx, file_name in enumerate(os.listdir(input_dir)):
    if file_name.lower().endswith(('.png', '.jpg', '.jpeg')):  # 이미지 파일만 처리
        img_path = os.path.join(input_dir, file_name) # 파이1이 찍은 사진들이 있는 디렉토리
        img = cv2.imread(img_path)

        if img is None:
            print(f"이미지를 불러오지 못했습니다: {file_name}")
            continue

        # 그레이스케일 변환 -> LBPHFace는 그레이 사용
        gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)

        # 얼굴 인식
        faces = face_cascade.detectMultiScale(
            gray,
            scaleFactor=1.05,
            minNeighbors=7,
            minSize=(70, 70)
        )
        if len(faces) > 0:
            faces = sorted(faces, key = lambda x: x[2] * x[3], reverse=True)
            largest_face = faces[0]
            x, y, w, h = largest_face
            face_img = gray[y:y+h, x:x+w]

            base_name = os.path.splitext(file_name)[0]
            base_name_list = base_name.split('.')  # ['학번', '1']

            id = base_name_list[0]  # 학번 id로 학습하도록 저장
            face_path = os.path.join(output_dir, f"{id}.{idx}.jpg")
            cv2.imwrite(face_path, face_img) # 파이1에서 찍은 본래 이미지를 얼굴 만큼만 잘라서 저장

            augmented_img = augment_image(face_img)
            augmented_path = os.path.join(output_dir, f"{id}.{idx}.aug.jpg") # 파이1에서 찍은 본래 이미지를 얼굴 만큼만 잘른 후 이미지 증강 후 저장 
            # -> output_dir이 같기 때문에 증강 이미지와 본래 이미지가 같은 directory에 저장될 예정, 증강 이미지는 끝에 .aug만 다름
            cv2.imwrite(augmented_path, augmented_img)



######------------------------------------------------------------------- dataset 만들기

if not os.path.exists(save_model_dir): # 학습 모델을 저장할  directory가 없을 때
    os.makedirs(save_model_dir)
    
def get_images_and_labels(path): # 학습시킬 때 학번과 이미지를 mapping시키기 위한 함수
    image_paths = [os.path.join(path, f) for f in os.listdir(path) if not f.startswith('.')]  # 숨김 파일 제외
    face_samples = [] # 학습할 얼굴 이미지들 저장
    ids = []

    for image_path in image_paths:
        pil_image = Image.open(image_path).convert('L') # 해당 이미지를 그레이 스케일로 변환
        image_np = np.array(pil_image, 'uint8') # 이미지를 배열로 변환

        id = int(os.path.split(image_path)[-1].split(".")[0]) # 파일명에서 학번 추출 -> 학번.이미지개수 이므로 0번 인덱스틑 학번 의미

        face_samples.append(image_np) # 이미지 파일을 배열로 변환한 값을 리스트에 저장
        ids.append(id)

    return face_samples, ids # 해당 이미지와 학번을 한 쌍으로 학습시킬 예정

print("\n[INFO] Training faces. Please wait...")
faces, ids = get_images_and_labels(output_dir)
recognizer.train(faces, np.array(ids)) # 이미지에 따른 학번을 쌍으로 학습 시킴

model_path = os.path.join(save_model_dir, 'trainer.yml')
recognizer.write(model_path)
print(f"\n[INFO] Model trained and saved at {model_path}")
######------------------------------------------------------------------- 학습

