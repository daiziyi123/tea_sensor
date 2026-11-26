FROM python:3.12-slim
WORKDIR /srv/tea
ENV PYTHONDONTWRITEBYTECODE=1 PYTHONUNBUFFERED=1
COPY requirements.txt ./
RUN pip install --no-cache-dir -r requirements.txt && useradd --uid 10001 --create-home tea
COPY app ./app
COPY web ./web
RUN mkdir -p data && chown -R tea:tea /srv/tea
USER tea
EXPOSE 8000 9000
CMD ["python", "-m", "app.main"]
