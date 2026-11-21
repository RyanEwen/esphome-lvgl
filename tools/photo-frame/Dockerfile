FROM python:3.13-slim
RUN pip install --no-cache-dir pillow
COPY app.py /app/app.py
USER nobody
CMD ["python", "-u", "/app/app.py"]
